#ifndef RESP_H
#define RESP_H

#include <string>
#include <vector>

class RESP
{
public:
    // Parse one command from data buffer, returning tokens and setting consumed_bytes
    static std::vector<std::string> parseOne(const std::string &data, size_t &consumed_bytes);

    // Parse all commands (legacy helper or single batch)
    static std::vector<std::string> parse(const std::string &data);

    // Encode responses to RESP format
    static std::string encodeSimpleString(const std::string &str);
    static std::string encodeError(const std::string &err);
    static std::string encodeInteger(long long num);
    static std::string encodeBulkString(const std::string &str);
    static std::string encodeNullBulkString();
    static std::string encodeArray(const std::vector<std::string> &items);
};

#endif
