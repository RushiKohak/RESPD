#include "resp.h"
#include <sstream>

static std::vector<std::string> parsePlainText(const std::string &data)
{
    std::vector<std::string> result;

    std::string trimmed = data;
    while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == '\n'))
    {
        trimmed.pop_back();
    }

    std::istringstream iss(trimmed);
    std::string word;
    while (iss >> word)
    {
        result.push_back(word);
    }

    return result;
}

std::vector<std::string> RESP::parse(const std::string &data)
{
    std::vector<std::string> result;
    size_t pos = 0;

    if (data.empty())
    {
        return result;
    }

    if (data[0] != '*')
    {
        return parsePlainText(data);
    }

    pos = 1;
    size_t end = data.find("\r\n", pos);
    if (end == std::string::npos)
    {
        return result;
    }

    int arrayLen = 0;
    try {
        arrayLen = std::stoi(data.substr(pos, end - pos));
    } catch (...) {
        return result;
    }
    pos = end + 2;

    for (int i = 0; i < arrayLen; i++)
    {
        if (pos >= data.length() || data[pos] != '$')
        {
            break;
        }

        pos++; // Skip '$'
        end = data.find("\r\n", pos);
        if (end == std::string::npos)
        {
            break;
        }

        int bulkLen = 0;
        try {
            bulkLen = std::stoi(data.substr(pos, end - pos));
        } catch (...) {
            break;
        }
        pos = end + 2;

        if (bulkLen < 0 || pos + static_cast<size_t>(bulkLen) > data.length())
        {
            break;
        }

        std::string bulk = data.substr(pos, bulkLen);
        result.push_back(bulk);
        pos += static_cast<size_t>(bulkLen) + 2; // Skip bulk string and \r\n
    }

    return result;
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
