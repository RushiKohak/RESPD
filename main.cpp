#include "db.h"
#include <iostream>
#include <sstream>
#include <vector>

static std::vector<std::string> split(const std::string &line)
{
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string word;

    while (iss >> word)
        tokens.push_back(word);

    return tokens;
}

int main()
{
    Db redis;

    std::string line;

    while (true)
    {
        std::cout << "> ";
        if (!std::getline(std::cin, line))
            break;

        auto tokens = split(line);
        if (tokens.empty())
            continue;

        const std::string &cmd = tokens[0];

        if (cmd == "SET" || cmd == "set")
        {
            if (tokens.size() < 3)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            std::string value;
            for (size_t i = 2; i < tokens.size(); i++)
            {
                if (i > 2) value += " ";
                value += tokens[i];
            }
            redis.set(tokens[1], value);
            std::cout << "OK" << std::endl;
        }
        else if (cmd == "GET" || cmd == "get")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            GetResult res = redis.get(tokens[1]);
            if (res.status == GetResult::NIL)
                std::cout << "nil" << std::endl;
            else if (res.status == GetResult::WRONG_TYPE)
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << res.value << std::endl;
        }
        else if (cmd == "DEL" || cmd == "del")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            long long count = redis.del(tokens[1]);
            std::cout << "(integer) " << count << std::endl;
        }
        else if (cmd == "EXISTS" || cmd == "exists")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            bool ex = redis.exists(tokens[1]);
            std::cout << "(integer) " << (ex ? 1 : 0) << std::endl;
        }
        else if (cmd == "INCR" || cmd == "incr")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            auto res = redis.incr(tokens[1]);
            if (!res.has_value())
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << "(integer) " << res.value() << std::endl;
        }
        else if (cmd == "INCRBY" || cmd == "incrby")
        {
            if (tokens.size() < 3)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            long long amt = std::stoll(tokens[2]);
            auto res = redis.incrby(tokens[1], amt);
            if (!res.has_value())
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << "(integer) " << res.value() << std::endl;
        }
        else if (cmd == "DECR" || cmd == "decr")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            auto res = redis.decr(tokens[1]);
            if (!res.has_value())
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << "(integer) " << res.value() << std::endl;
        }
        else if (cmd == "DECRBY" || cmd == "decrby")
        {
            if (tokens.size() < 3)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            long long amt = std::stoll(tokens[2]);
            auto res = redis.decrby(tokens[1], amt);
            if (!res.has_value())
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << "(integer) " << res.value() << std::endl;
        }
        else if (cmd == "TYPE" || cmd == "type")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            std::cout << redis.type(tokens[1]) << std::endl;
        }
        else if (cmd == "EXPIRE" || cmd == "expire")
        {
            if (tokens.size() < 3)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            long long seconds = std::stoll(tokens[2]);
            bool ok = redis.expire(tokens[1], seconds);
            std::cout << "(integer) " << (ok ? 1 : 0) << std::endl;
        }
        else if (cmd == "TTL" || cmd == "ttl")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            std::cout << "(integer) " << redis.ttl(tokens[1]) << std::endl;
        }
        else if (cmd == "PERSIST" || cmd == "persist")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            bool ok = redis.persist(tokens[1]);
            std::cout << "(integer) " << (ok ? 1 : 0) << std::endl;
        }
        else if (cmd == "APPEND" || cmd == "append")
        {
            if (tokens.size() < 3)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            std::string value;
            for (size_t i = 2; i < tokens.size(); i++)
            {
                if (i > 2) value += " ";
                value += tokens[i];
            }
            long long len = redis.append(tokens[1], value);
            if (len < 0)
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << "(integer) " << len << std::endl;
        }
        else if (cmd == "STRLEN" || cmd == "strlen")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            long long len = redis.strlen(tokens[1]);
            if (len < 0)
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << "(integer) " << len << std::endl;
        }
        else if (cmd == "MGET" || cmd == "mget")
        {
            if (tokens.size() < 2)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            std::vector<std::string> keys(tokens.begin() + 1, tokens.end());
            auto vals = redis.mget(keys);
            for (size_t i = 0; i < vals.size(); i++)
            {
                std::cout << (i + 1) << ") ";
                if (!vals[i].has_value())
                    std::cout << "(nil)" << std::endl;
                else
                    std::cout << "\"" << vals[i].value() << "\"" << std::endl;
            }
        }
        else if (cmd == "MSET" || cmd == "mset")
        {
            if (tokens.size() < 3 || tokens.size() % 2 == 0)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            std::vector<std::string> keyvals(tokens.begin() + 1, tokens.end());
            if (redis.mset(keyvals))
                std::cout << "OK" << std::endl;
            else
                std::cout << "(error) wrong number of arguments" << std::endl;
        }
        else if (cmd == "GETRANGE" || cmd == "getrange")
        {
            if (tokens.size() < 4)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            long long start = std::stoll(tokens[2]);
            long long end = std::stoll(tokens[3]);
            auto res = redis.getrange(tokens[1], start, end);
            if (!res.has_value())
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << "\"" << res.value() << "\"" << std::endl;
        }
        else if (cmd == "SETRANGE" || cmd == "setrange")
        {
            if (tokens.size() < 4)
            {
                std::cout << "(error) wrong number of arguments" << std::endl;
                continue;
            }
            long long offset = std::stoll(tokens[2]);
            std::string value;
            for (size_t i = 3; i < tokens.size(); i++)
            {
                if (i > 3) value += " ";
                value += tokens[i];
            }
            long long len = redis.setrange(tokens[1], offset, value);
            if (len < 0)
                std::cout << "(error) WRONGTYPE" << std::endl;
            else
                std::cout << "(integer) " << len << std::endl;
        }
        else if (cmd == "SAVE" || cmd == "save")
        {
            bool ok = redis.saveRDB();
            redis.startNewAOF();
            if (ok)
                std::cout << "OK" << std::endl;
            else
                std::cout << "(error) Failed to save" << std::endl;
        }
        else if (cmd == "QUIT" || cmd == "quit" || cmd == "EXIT" || cmd == "exit")
        {
            break;
        }
        else
        {
            std::cout << "(error) unknown command" << std::endl;
        }
    }
    return 0;
}
