#include "db.h"
#include <cstdio>
#include <iostream>
#include <vector>
#include <sstream>

static std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '\"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

static std::string jsonUnescape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            switch (s[i + 1])
            {
            case '\"': out += '\"'; i++; break;
            case '\\': out += '\\'; i++; break;
            case 'b': out += '\b'; i++; break;
            case 'f': out += '\f'; i++; break;
            case 'n': out += '\n'; i++; break;
            case 'r': out += '\r'; i++; break;
            case 't': out += '\t'; i++; break;
            case 'u':
                if (i + 5 < s.size())
                {
                    int code;
                    if (sscanf(s.c_str() + i + 2, "%04x", &code) == 1)
                    {
                        out += static_cast<char>(code);
                        i += 5;
                    }
                }
                break;
            default: out += s[i]; break;
            }
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

Db::Db(const std::string &rdb_file, const std::string &aof_file, int auto_save_interval)
    : rdb_filename_(rdb_file), aof_filename_(aof_file), auto_save_interval_(auto_save_interval)
{
    loadRDB();
    loadAOF();

    aof_file_.open(aof_filename_, std::ios::app);
    last_save_time_ = std::chrono::steady_clock::now();

    std::cout << "# Database loaded from " << rdb_filename_
              << " and " << aof_filename_ << std::endl;
}

Db::~Db()
{
    if (aof_file_.is_open())
    {
        aof_file_.close();
    }
    saveRDB();

    std::cout << "# Database saved to " << rdb_filename_ << std::endl;
}

void Db::logToAOF(const std::string &command)
{
    if (aof_file_.is_open())
    {
        aof_file_ << command << std::endl;
        aof_file_.flush();
    }
}

void Db::checkAutoSave()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time_).count();

    if (elapsed >= auto_save_interval_)
    {
        std::cout << "# Auto-saving..." << std::endl;
        saveRDB();
        startNewAOF();
        last_save_time_ = now;
    }
}

bool Db::saveRDB()
{
    std::ofstream file(rdb_filename_);
    if (!file.is_open())
    {
        return false;
    }

    file << "{\n";
    bool first = true;

    for (const auto &pair : bucketstore)
    {
        const std::string &key = pair.first;
        const Value &val = pair.second;

        if (val.isExpired())
        {
            continue;
        }

        if (!first)
        {
            file << ",\n";
        }
        first = false;

        file << "  \"" << jsonEscape(key) << "\": {\n";

        if (val.type == ValueType::STRING)
        {
            file << "    \"type\": \"string\",\n";
            file << "    \"value\": \"" << jsonEscape(val.str()) << "\"";
        }
        else if (val.type == ValueType::INTEGER)
        {
            file << "    \"type\": \"integer\",\n";
            file << "    \"value\": " << val.integer();
        }
        else if (val.type == ValueType::LIST)
        {
            file << "    \"type\": \"list\",\n";
            file << "    \"value\": [";
            bool firstElem = true;
            for (const auto &elem : val.list())
            {
                if (!firstElem) file << ", ";
                firstElem = false;
                file << "\"" << jsonEscape(elem) << "\"";
            }
            file << "]";
        }
        else if (val.type == ValueType::SET)
        {
            file << "    \"type\": \"set\",\n";
            file << "    \"value\": [";
            bool firstElem = true;
            for (const auto &elem : val.set())
            {
                if (!firstElem) file << ", ";
                firstElem = false;
                file << "\"" << jsonEscape(elem) << "\"";
            }
            file << "]";
        }
        else if (val.type == ValueType::HASH)
        {
            file << "    \"type\": \"hash\",\n";
            file << "    \"value\": {";
            bool firstElem = true;
            for (const auto &entry : val.hash())
            {
                if (!firstElem) file << ", ";
                firstElem = false;
                file << "\"" << jsonEscape(entry.first) << "\": \"" << jsonEscape(entry.second) << "\"";
            }
            file << "}";
        }

        long long ttl_val = val.getTTL();
        if (ttl_val >= 0)
        {
            file << ",\n    \"ttl\": " << ttl_val;
        }
        else
        {
            file << ",\n    \"ttl\": -1";
        }

        file << "\n  }";
    }

    file << "\n}\n";
    file.close();
    return true;
}

bool Db::loadRDB()
{
    std::ifstream file(rdb_filename_);
    if (!file.is_open())
    {
        return false;
    }

    std::string line;
    std::string currentKey;
    std::string valueType;
    std::string valueStr;
    long long valueInt = 0;
    long long ttl = -1;

    while (std::getline(file, line))
    {
        size_t start = line.find_first_not_of(" \t");
        size_t end = line.find_last_not_of(" \t,");
        if (start == std::string::npos)
            continue;
        line = line.substr(start, end - start + 1);

        if (line == "{" || line == "}" || line.empty())
            continue;

        if (line.find("\":") != std::string::npos && line.back() == '{')
        {
            size_t keyStart = line.find('"') + 1;
            size_t keyEnd = line.find('"', keyStart);
            currentKey = line.substr(keyStart, keyEnd - keyStart);
            continue;
        }

        if (line.find("\"type\":") != std::string::npos)
        {
            size_t valStart = line.find(": \"") + 3;
            size_t valEnd = line.find('"', valStart);
            valueType = line.substr(valStart, valEnd - valStart);
            continue;
        }

        if (line.find("\"value\":") != std::string::npos)
        {
            if (valueType == "string")
            {
                size_t valStart = line.find(": \"") + 3;
                size_t valEnd = line.find('"', valStart);
                valueStr = jsonUnescape(line.substr(valStart, valEnd - valStart));
            }
            else if (valueType == "integer")
            {
                size_t valStart = line.find(": ") + 2;
                valueInt = std::stoll(line.substr(valStart));
            }
            continue;
        }

        if (line.find("\"ttl\":") != std::string::npos)
        {
            size_t valStart = line.find(": ") + 2;
            std::string ttlStr = line.substr(valStart);
            if (ttlStr != "null" && ttlStr != "-1")
            {
                ttl = std::stoll(ttlStr);
            }
            else
            {
                ttl = -1;
            }

            if (!currentKey.empty())
            {
                Value v;
                if (valueType == "string")
                {
                    v = Value(valueStr);
                }
                else if (valueType == "integer")
                {
                    v = Value(valueInt);
                }

                if (ttl > 0)
                {
                    v.setExpiration(ttl);
                }

                bucketstore[currentKey] = v;

                currentKey.clear();
                valueType.clear();
                valueStr.clear();
                valueInt = 0;
                ttl = -1;
            }
        }
    }

    file.close();
    return true;
}

bool Db::loadAOF()
{
    std::ifstream file(aof_filename_);
    if (!file.is_open())
    {
        return false;
    }

    std::string line;
    int commands_replayed = 0;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string word;

        while (iss >> word)
            tokens.push_back(word);

        if (tokens.empty())
            continue;

        const std::string &cmd = tokens[0];

        if (cmd == "SET" && tokens.size() >= 3)
        {
            std::string key = tokens[1];
            std::string value;
            for (size_t i = 2; i < tokens.size(); i++)
            {
                if (i > 2) value += " ";
                value += tokens[i];
            }
            bucketstore[key] = Value(value);
        }
        else if (cmd == "DEL" && tokens.size() >= 2)
        {
            bucketstore.erase(tokens[1]);
        }
        else if (cmd == "INCR" && tokens.size() >= 2)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                bucketstore[tokens[1]] = Value(1);
            }
            else if (it->second.type == ValueType::INTEGER)
            {
                it->second.integer() += 1;
            }
        }
        else if (cmd == "INCRBY" && tokens.size() >= 3)
        {
            long long amount = std::stoll(tokens[2]);
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                bucketstore[tokens[1]] = Value(amount);
            }
            else if (it->second.type == ValueType::INTEGER)
            {
                it->second.integer() += amount;
            }
        }
        else if (cmd == "DECR" && tokens.size() >= 2)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                bucketstore[tokens[1]] = Value(-1);
            }
            else if (it->second.type == ValueType::INTEGER)
            {
                it->second.integer() -= 1;
            }
        }
        else if (cmd == "DECRBY" && tokens.size() >= 3)
        {
            long long amount = std::stoll(tokens[2]);
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                bucketstore[tokens[1]] = Value(-amount);
            }
            else if (it->second.type == ValueType::INTEGER)
            {
                it->second.integer() -= amount;
            }
        }
        else if (cmd == "APPEND" && tokens.size() >= 3)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                bucketstore[tokens[1]] = Value(tokens[2]);
            }
            else if (it->second.type == ValueType::STRING)
            {
                it->second.str().append(tokens[2]);
            }
        }
        else if (cmd == "SETRANGE" && tokens.size() >= 4)
        {
            long long offset = std::stoll(tokens[2]);
            const std::string &value = tokens[3];
            auto it = bucketstore.find(tokens[1]);

            if (it == bucketstore.end())
            {
                std::string newStr(offset + value.length(), '\0');
                for (size_t i = 0; i < value.length(); i++)
                {
                    newStr[offset + i] = value[i];
                }
                bucketstore[tokens[1]] = Value(newStr);
            }
            else if (it->second.type == ValueType::STRING)
            {
                if (offset + value.length() > it->second.str().length())
                {
                    it->second.str().resize(offset + value.length(), '\0');
                }
                for (size_t i = 0; i < value.length(); i++)
                {
                    it->second.str()[offset + i] = value[i];
                }
            }
        }
        else if (cmd == "EXPIRE" && tokens.size() >= 3)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it != bucketstore.end())
            {
                long long seconds = std::stoll(tokens[2]);
                it->second.setExpiration(seconds);
            }
        }
        else if (cmd == "PERSIST" && tokens.size() >= 2)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it != bucketstore.end())
            {
                it->second.persist();
            }
        }
        else if (cmd == "LPUSH" && tokens.size() >= 3)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                RedisList nl;
                nl.push_front(tokens[2]);
                bucketstore[tokens[1]] = Value(nl);
            }
            else if (it->second.type == ValueType::LIST)
            {
                it->second.list().push_front(tokens[2]);
            }
        }
        else if (cmd == "RPUSH" && tokens.size() >= 3)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                RedisList nl;
                nl.push_back(tokens[2]);
                bucketstore[tokens[1]] = Value(nl);
            }
            else if (it->second.type == ValueType::LIST)
            {
                it->second.list().push_back(tokens[2]);
            }
        }
        else if (cmd == "LPOP" && tokens.size() >= 2)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it != bucketstore.end() && it->second.type == ValueType::LIST && !it->second.list().empty())
            {
                it->second.list().pop_front();
                if (it->second.list().empty()) bucketstore.erase(it);
            }
        }
        else if (cmd == "RPOP" && tokens.size() >= 2)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it != bucketstore.end() && it->second.type == ValueType::LIST && !it->second.list().empty())
            {
                it->second.list().pop_back();
                if (it->second.list().empty()) bucketstore.erase(it);
            }
        }
        else if (cmd == "LSET" && tokens.size() >= 4)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it != bucketstore.end() && it->second.type == ValueType::LIST)
            {
                long long idx = std::stoll(tokens[2]);
                if (idx >= 0 && static_cast<size_t>(idx) < it->second.list().size())
                {
                    it->second.list()[idx] = tokens[3];
                }
            }
        }
        else if (cmd == "SADD" && tokens.size() >= 3)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                RedisSet ns;
                ns.insert(tokens[2]);
                bucketstore[tokens[1]] = Value(ns);
            }
            else if (it->second.type == ValueType::SET)
            {
                it->second.set().insert(tokens[2]);
            }
        }
        else if (cmd == "SREM" && tokens.size() >= 3)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it != bucketstore.end() && it->second.type == ValueType::SET)
            {
                it->second.set().erase(tokens[2]);
                if (it->second.set().empty()) bucketstore.erase(it);
            }
        }
        else if (cmd == "HSET" && tokens.size() >= 4)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it == bucketstore.end())
            {
                RedisHash nh;
                nh[tokens[2]] = tokens[3];
                bucketstore[tokens[1]] = Value(nh);
            }
            else if (it->second.type == ValueType::HASH)
            {
                it->second.hash()[tokens[2]] = tokens[3];
            }
        }
        else if (cmd == "HDEL" && tokens.size() >= 3)
        {
            auto it = bucketstore.find(tokens[1]);
            if (it != bucketstore.end() && it->second.type == ValueType::HASH)
            {
                it->second.hash().erase(tokens[2]);
                if (it->second.hash().empty()) bucketstore.erase(it);
            }
        }

        commands_replayed++;
    }

    file.close();

    if (commands_replayed > 0)
        std::cout << "# Replayed " << commands_replayed << " commands from AOF" << std::endl;

    return true;
}

void Db::startNewAOF()
{
    if (aof_file_.is_open())
    {
        aof_file_.close();
    }

    std::remove(aof_filename_.c_str());
    aof_file_.open(aof_filename_, std::ios::app);
    std::cout << "# Started new AOF" << std::endl;
}

void Db::cleanupIfExpired(const std::string &key)
{
    auto it = bucketstore.find(key);
    if (it != bucketstore.end() && it->second.isExpired())
    {
        bucketstore.erase(it);
    }
}

// ============== String Commands ==============

void Db::set(const std::string &key, const std::string &value)
{
    bucketstore[key] = Value(value);
    logToAOF("SET " + key + " " + value);
    checkAutoSave();
}

GetResult Db::get(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return {GetResult::NIL, ""};
    }

    Value &v = it->second;
    if (v.type == ValueType::STRING)
    {
        return {GetResult::OK, v.str()};
    }
    else if (v.type == ValueType::INTEGER)
    {
        return {GetResult::OK, std::to_string(v.integer())};
    }
    return {GetResult::WRONG_TYPE, ""};
}

long long Db::del(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }

    bucketstore.erase(it);
    logToAOF("DEL " + key);
    checkAutoSave();
    return 1;
}

bool Db::exists(const std::string &key)
{
    cleanupIfExpired(key);
    return bucketstore.find(key) != bucketstore.end();
}

std::optional<long long> Db::incr(const std::string &key)
{
    return incrby(key, 1);
}

std::optional<long long> Db::incrby(const std::string &key, long long amount)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);

    if (it == bucketstore.end())
    {
        bucketstore[key] = Value(amount);
        logToAOF("INCRBY " + key + " " + std::to_string(amount));
        checkAutoSave();
        return amount;
    }

    Value &v = it->second;
    if (v.type != ValueType::INTEGER)
    {
        return std::nullopt;
    }

    v.integer() += amount;
    logToAOF("INCRBY " + key + " " + std::to_string(amount));
    checkAutoSave();
    return v.integer();
}

std::optional<long long> Db::decr(const std::string &key)
{
    return incrby(key, -1);
}

std::optional<long long> Db::decrby(const std::string &key, long long amount)
{
    return incrby(key, -amount);
}

long long Db::append(const std::string &key, const std::string &value)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);

    if (it == bucketstore.end())
    {
        bucketstore[key] = Value(value);
        logToAOF("APPEND " + key + " " + value);
        checkAutoSave();
        return static_cast<long long>(value.length());
    }

    Value &v = it->second;
    if (v.type != ValueType::STRING)
    {
        return -1;
    }

    v.str().append(value);
    logToAOF("APPEND " + key + " " + value);
    checkAutoSave();
    return static_cast<long long>(v.str().length());
}

long long Db::strlen(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }

    Value &v = it->second;
    if (v.type != ValueType::STRING)
    {
        return -1;
    }
    return static_cast<long long>(v.str().length());
}

std::vector<std::optional<std::string>> Db::mget(const std::vector<std::string> &keys)
{
    std::vector<std::optional<std::string>> results;
    results.reserve(keys.size());

    for (const auto &k : keys)
    {
        cleanupIfExpired(k);
        auto it = bucketstore.find(k);
        if (it == bucketstore.end())
        {
            results.push_back(std::nullopt);
            continue;
        }

        Value &v = it->second;
        if (v.type == ValueType::STRING)
        {
            results.push_back(v.str());
        }
        else if (v.type == ValueType::INTEGER)
        {
            results.push_back(std::to_string(v.integer()));
        }
        else
        {
            results.push_back(std::nullopt);
        }
    }
    return results;
}

bool Db::mset(const std::vector<std::string> &keyvals)
{
    if (keyvals.size() % 2 != 0)
    {
        return false;
    }

    for (size_t i = 0; i < keyvals.size(); i += 2)
    {
        const std::string &key = keyvals[i];
        const std::string &value = keyvals[i + 1];

        bucketstore[key] = Value(value);
        logToAOF("SET " + key + " " + value);
        checkAutoSave();
    }
    return true;
}

std::optional<std::string> Db::getrange(const std::string &key, long long start, long long end)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return "";
    }
    Value &v = it->second;
    if (v.type != ValueType::STRING)
    {
        return std::nullopt;
    }

    long long len = static_cast<long long>(v.str().length());
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    if (start < 0) start = 0;
    if (end >= len) end = len - 1;
    if (start > end || start >= len)
    {
        return "";
    }

    return v.str().substr(start, end - start + 1);
}

long long Db::setrange(const std::string &key, long long offset, const std::string &value)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);

    if (it == bucketstore.end())
    {
        std::string newStr(offset + value.length(), '\0');
        for (size_t i = 0; i < value.length(); i++)
        {
            newStr[offset + i] = value[i];
        }
        bucketstore[key] = Value(newStr);
        logToAOF("SETRANGE " + key + " " + std::to_string(offset) + " " + value);
        checkAutoSave();
        return static_cast<long long>(newStr.length());
    }

    Value &v = it->second;
    if (v.type != ValueType::STRING)
    {
        return -1;
    }

    if (offset + value.length() > v.str().length())
    {
        v.str().resize(offset + value.length(), '\0');
    }

    for (size_t i = 0; i < value.length(); i++)
    {
        v.str()[offset + i] = value[i];
    }
    logToAOF("SETRANGE " + key + " " + std::to_string(offset) + " " + value);
    checkAutoSave();
    return static_cast<long long>(v.str().length());
}

// ============== Key Commands ==============

bool Db::expire(const std::string &key, long long seconds)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return false;
    }

    it->second.setExpiration(seconds);
    logToAOF("EXPIRE " + key + " " + std::to_string(seconds));
    checkAutoSave();
    return true;
}

long long Db::ttl(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return -2;
    }
    return it->second.getTTL();
}

bool Db::persist(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end() || it->second.getTTL() == -1)
    {
        return false;
    }

    it->second.persist();
    logToAOF("PERSIST " + key);
    checkAutoSave();
    return true;
}

std::string Db::type(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return "none";
    }

    Value &v = it->second;
    switch (v.type)
    {
    case ValueType::STRING:
    case ValueType::INTEGER:
        return "string";
    case ValueType::LIST:
        return "list";
    case ValueType::SET:
        return "set";
    case ValueType::HASH:
        return "hash";
    default:
        return "none";
    }
}

// ============== List Commands ==============

long long Db::lpush(const std::string &key, const std::vector<std::string> &values)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);

    if (it == bucketstore.end())
    {
        RedisList newList;
        for (auto rit = values.rbegin(); rit != values.rend(); ++rit)
        {
            newList.push_front(*rit);
        }
        bucketstore[key] = Value(newList);
    }
    else if (it->second.type != ValueType::LIST)
    {
        return -1;
    }
    else
    {
        for (auto rit = values.rbegin(); rit != values.rend(); ++rit)
        {
            it->second.list().push_front(*rit);
        }
    }

    for (const auto &val : values)
    {
        logToAOF("LPUSH " + key + " " + val);
    }
    checkAutoSave();

    return static_cast<long long>(bucketstore[key].list().size());
}

long long Db::rpush(const std::string &key, const std::vector<std::string> &values)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);

    if (it == bucketstore.end())
    {
        RedisList newList(values.begin(), values.end());
        bucketstore[key] = Value(newList);
    }
    else if (it->second.type != ValueType::LIST)
    {
        return -1;
    }
    else
    {
        for (const auto &val : values)
        {
            it->second.list().push_back(val);
        }
    }

    for (const auto &val : values)
    {
        logToAOF("RPUSH " + key + " " + val);
    }
    checkAutoSave();

    return static_cast<long long>(bucketstore[key].list().size());
}

GetResult Db::lpop(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return {GetResult::NIL, ""};
    }
    if (it->second.type != ValueType::LIST)
    {
        return {GetResult::WRONG_TYPE, ""};
    }
    if (it->second.list().empty())
    {
        return {GetResult::NIL, ""};
    }

    std::string result = it->second.list().front();
    it->second.list().pop_front();

    if (it->second.list().empty())
    {
        bucketstore.erase(it);
    }

    logToAOF("LPOP " + key);
    checkAutoSave();
    return {GetResult::OK, result};
}

GetResult Db::rpop(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return {GetResult::NIL, ""};
    }
    if (it->second.type != ValueType::LIST)
    {
        return {GetResult::WRONG_TYPE, ""};
    }
    if (it->second.list().empty())
    {
        return {GetResult::NIL, ""};
    }

    std::string result = it->second.list().back();
    it->second.list().pop_back();

    if (it->second.list().empty())
    {
        bucketstore.erase(it);
    }

    logToAOF("RPOP " + key);
    checkAutoSave();
    return {GetResult::OK, result};
}

long long Db::llen(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }
    if (it->second.type != ValueType::LIST)
    {
        return -1;
    }
    return static_cast<long long>(it->second.list().size());
}

RangeResult Db::lrange(const std::string &key, long long start, long long stop)
{
    cleanupIfExpired(key);
    RangeResult result;
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return result;
    }
    if (it->second.type != ValueType::LIST)
    {
        result.wrong_type = true;
        return result;
    }

    const RedisList &lst = it->second.list();
    long long len = static_cast<long long>(lst.size());
    if (start < 0) start = len + start;
    if (stop < 0) stop = len + stop;
    if (start < 0) start = 0;
    if (stop >= len) stop = len - 1;

    if (start > stop || start >= len)
    {
        return result;
    }

    for (long long i = start; i <= stop; i++)
    {
        result.values.push_back(lst[i]);
    }
    return result;
}

GetResult Db::lindex(const std::string &key, long long index)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return {GetResult::NIL, ""};
    }
    if (it->second.type != ValueType::LIST)
    {
        return {GetResult::WRONG_TYPE, ""};
    }

    const RedisList &lst = it->second.list();
    long long len = static_cast<long long>(lst.size());
    if (index < 0) index = len + index;

    if (index < 0 || index >= len)
    {
        return {GetResult::NIL, ""};
    }
    return {GetResult::OK, lst[index]};
}

LSetStatus Db::lset(const std::string &key, long long index, const std::string &value)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return LSetStatus::NO_SUCH_KEY;
    }
    if (it->second.type != ValueType::LIST)
    {
        return LSetStatus::WRONG_TYPE;
    }

    RedisList &lst = it->second.list();
    long long len = static_cast<long long>(lst.size());
    if (index < 0) index = len + index;

    if (index < 0 || index >= len)
    {
        return LSetStatus::INDEX_OUT_OF_RANGE;
    }

    lst[index] = value;
    logToAOF("LSET " + key + " " + std::to_string(index) + " " + value);
    checkAutoSave();
    return LSetStatus::OK;
}

// ============== Set Commands ==============

long long Db::sadd(const std::string &key, const std::vector<std::string> &members)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    long long added = 0;

    if (it == bucketstore.end())
    {
        RedisSet newSet(members.begin(), members.end());
        added = static_cast<long long>(newSet.size());
        bucketstore[key] = Value(newSet);
    }
    else if (it->second.type != ValueType::SET)
    {
        return -1;
    }
    else
    {
        for (const auto &member : members)
        {
            auto result = it->second.set().insert(member);
            if (result.second) added++;
        }
    }

    for (const auto &member : members)
    {
        logToAOF("SADD " + key + " " + member);
    }
    checkAutoSave();
    return added;
}

long long Db::srem(const std::string &key, const std::string &member)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }
    if (it->second.type != ValueType::SET)
    {
        return -1;
    }

    long long removed = static_cast<long long>(it->second.set().erase(member));
    if (it->second.set().empty())
    {
        bucketstore.erase(it);
    }

    if (removed > 0)
    {
        logToAOF("SREM " + key + " " + member);
        checkAutoSave();
    }
    return removed;
}

RangeResult Db::smembers(const std::string &key)
{
    cleanupIfExpired(key);
    RangeResult result;
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return result;
    }
    if (it->second.type != ValueType::SET)
    {
        result.wrong_type = true;
        return result;
    }

    for (const auto &member : it->second.set())
    {
        result.values.push_back(member);
    }
    return result;
}

int Db::sismember(const std::string &key, const std::string &member)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }
    if (it->second.type != ValueType::SET)
    {
        return -1;
    }
    return it->second.set().count(member) > 0 ? 1 : 0;
}

long long Db::scard(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }
    if (it->second.type != ValueType::SET)
    {
        return -1;
    }
    return static_cast<long long>(it->second.set().size());
}

// ============== Hash Commands ==============

int Db::hset(const std::string &key, const std::string &field, const std::string &value)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    int isNew = 0;

    if (it == bucketstore.end())
    {
        RedisHash newHash;
        newHash[field] = value;
        bucketstore[key] = Value(newHash);
        isNew = 1;
    }
    else if (it->second.type != ValueType::HASH)
    {
        return -1;
    }
    else
    {
        isNew = (it->second.hash().find(field) == it->second.hash().end()) ? 1 : 0;
        it->second.hash()[field] = value;
    }

    logToAOF("HSET " + key + " " + field + " " + value);
    checkAutoSave();
    return isNew;
}

GetResult Db::hget(const std::string &key, const std::string &field)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return {GetResult::NIL, ""};
    }
    if (it->second.type != ValueType::HASH)
    {
        return {GetResult::WRONG_TYPE, ""};
    }

    auto fieldIt = it->second.hash().find(field);
    if (fieldIt == it->second.hash().end())
    {
        return {GetResult::NIL, ""};
    }
    return {GetResult::OK, fieldIt->second};
}

int Db::hdel(const std::string &key, const std::string &field)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }
    if (it->second.type != ValueType::HASH)
    {
        return -1;
    }

    int deleted = it->second.hash().erase(field) > 0 ? 1 : 0;
    if (it->second.hash().empty())
    {
        bucketstore.erase(it);
    }
    if (deleted)
    {
        logToAOF("HDEL " + key + " " + field);
        checkAutoSave();
    }
    return deleted;
}

HashGetAllResult Db::hgetall(const std::string &key)
{
    cleanupIfExpired(key);
    HashGetAllResult result;
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return result;
    }
    if (it->second.type != ValueType::HASH)
    {
        result.wrong_type = true;
        return result;
    }

    for (const auto &pair : it->second.hash())
    {
        result.entries.push_back(pair);
    }
    return result;
}

RangeResult Db::hkeys(const std::string &key)
{
    cleanupIfExpired(key);
    RangeResult result;
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return result;
    }
    if (it->second.type != ValueType::HASH)
    {
        result.wrong_type = true;
        return result;
    }

    for (const auto &pair : it->second.hash())
    {
        result.values.push_back(pair.first);
    }
    return result;
}

RangeResult Db::hvals(const std::string &key)
{
    cleanupIfExpired(key);
    RangeResult result;
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return result;
    }
    if (it->second.type != ValueType::HASH)
    {
        result.wrong_type = true;
        return result;
    }

    for (const auto &pair : it->second.hash())
    {
        result.values.push_back(pair.second);
    }
    return result;
}

long long Db::hlen(const std::string &key)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }
    if (it->second.type != ValueType::HASH)
    {
        return -1;
    }
    return static_cast<long long>(it->second.hash().size());
}

int Db::hexists(const std::string &key, const std::string &field)
{
    cleanupIfExpired(key);
    auto it = bucketstore.find(key);
    if (it == bucketstore.end())
    {
        return 0;
    }
    if (it->second.type != ValueType::HASH)
    {
        return -1;
    }
    return it->second.hash().count(field) > 0 ? 1 : 0;
}
