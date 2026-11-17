#include "resp.h"
#include <sstream>

std::vector<std::string> RESP::parseOne(const std::string &data, size_t &consumed_bytes)
{
    consumed_bytes = 0;
    if (data.empty())
    {
        return {};
    }

    // Check if this is RESP array format (starts with '*')
    if (data[0] == '*')
    {
        size_t pos = 1;
        size_t end = data.find("\r\n", pos);
        if (end == std::string::npos)
        {
            return {}; // Incomplete array header
        }

        int arrayLen = 0;
        try {
            arrayLen = std::stoi(data.substr(pos, end - pos));
        } catch (...) {
            consumed_bytes = end + 2;
            return {};
        }

        pos = end + 2;

        if (arrayLen <= 0)
        {
            consumed_bytes = pos;
            return {};
        }

        std::vector<std::string> result;
        result.reserve(arrayLen);

        for (int i = 0; i < arrayLen; i++)
        {
            if (pos >= data.length())
            {
                return {}; // Incomplete
            }

            if (data[pos] != '$')
            {
                // Malformed RESP element
                consumed_bytes = pos;
                return {};
            }

            pos++; // Skip '$'
            end = data.find("\r\n", pos);
            if (end == std::string::npos)
            {
                return {}; // Incomplete bulk header
            }

            int bulkLen = 0;
            try {
                bulkLen = std::stoi(data.substr(pos, end - pos));
            } catch (...) {
                consumed_bytes = end + 2;
                return {};
            }
            pos = end + 2;

            if (bulkLen < 0)
            {
                // Null bulk string
                result.push_back("");
                continue;
            }

            if (pos + static_cast<size_t>(bulkLen) + 2 > data.length())
            {
                return {}; // Incomplete bulk data or missing trailing \r\n
            }

            std::string bulk = data.substr(pos, bulkLen);
            result.push_back(bulk);
            pos += static_cast<size_t>(bulkLen) + 2; // Skip bulk string and \r\n
        }

        consumed_bytes = pos;
        return result;
    }

    // Otherwise, parse as inline / plain text command
    size_t end = data.find("\r\n");
    size_t crlf_len = 2;
    if (end == std::string::npos)
    {
        end = data.find("\n");
        crlf_len = 1;
    }

    if (end == std::string::npos)
    {
        return {}; // Incomplete line
    }

    std::string line = data.substr(0, end);
    consumed_bytes = end + crlf_len;

    std::vector<std::string> result;
    std::istringstream iss(line);
    std::string word;
    while (iss >> word)
    {
        result.push_back(word);
    }
    return result;
}

std::vector<std::string> RESP::parse(const std::string &data)
{
    size_t consumed = 0;
    return parseOne(data, consumed);
}

std::string RESP::encodeSimpleString(const std::string &str)
{
    return "+" + str + "\r\n";
}

std::string RESP::encodeError(const std::string &err)
{
    return "-" + err + "\r\n";
}

std::string RESP::encodeInteger(long long num)
{
    return ":" + std::to_string(num) + "\r\n";
}

std::string RESP::encodeBulkString(const std::string &str)
{
    return "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
}

std::string RESP::encodeNullBulkString()
{
    return "$-1\r\n";
}

std::string RESP::encodeArray(const std::vector<std::string> &items)
{
    std::string result = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto &item : items)
    {
        result += encodeBulkString(item);
    }
    return result;
}
