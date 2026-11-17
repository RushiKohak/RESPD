#include "server.h"
#include "resp.h"
#include <iostream>
#include <cstring>
#include <sstream>

Server::Server(Db &db, int port) : db_(db), port_(port), running_(false)
{
    server_socket_ = INVALID_SOCKET_HANDLE;
}

Server::~Server()
{
    stop();
}

void Server::start()
{
    init_network();

    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (!SOCKET_IS_VALID(server_socket_))
    {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }

    std::cout << "# Socket created: " << server_socket_ << std::endl;

    int opt = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)) < 0)
    {
        std::cerr << "Failed to set socket options" << std::endl;
        CLOSE_SOCKET(server_socket_);
        server_socket_ = INVALID_SOCKET_HANDLE;
        return;
    }

    std::cout << "# Socket options set (SO_REUSEADDR)" << std::endl;

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(server_socket_, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        std::cerr << "Failed to bind socket to port " << port_ << std::endl;
        CLOSE_SOCKET(server_socket_);
        server_socket_ = INVALID_SOCKET_HANDLE;
        return;
    }

    std::cout << "# Socket bound to 0.0.0.0:" << port_ << std::endl;

    if (listen(server_socket_, 10) < 0)
    {
        std::cerr << "Failed to listen on socket" << std::endl;
        CLOSE_SOCKET(server_socket_);
        server_socket_ = INVALID_SOCKET_HANDLE;
        return;
    }

    std::cout << "# Socket listening (backlog: 10)" << std::endl;

    running_ = true;
    std::cout << "# Respd server started on port " << port_ << std::endl;
    std::cout << "# Ready to accept connections" << std::endl;
    std::cout << "# Waiting for clients..." << std::endl;

    while (running_)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        socket_t client_socket = accept(server_socket_, (struct sockaddr *)&client_addr, &client_len);

        if (!SOCKET_IS_VALID(client_socket))
        {
            if (running_)
                std::cerr << "Failed to accept connection" << std::endl;
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        std::cout << "# Client connected from " << client_ip 
                  << ":" << ntohs(client_addr.sin_port) 
                  << " (socket: " << client_socket << ")" << std::endl;

        std::thread client_thread(&Server::handleClient, this, client_socket);
        client_thread.detach();
    }
}

void Server::handleClient(socket_t client_socket)
{
    char buffer[4096];

    while (running_)
    {
        std::memset(buffer, 0, sizeof(buffer));

        int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read <= 0)
        {
            break;
        }

        std::string data(buffer, bytes_read);

        std::vector<std::string> tokens = RESP::parse(data);

        if (tokens.empty())
        {
            std::string response = RESP::encodeError("ERR invalid command format");
            send(client_socket, response.c_str(), static_cast<int>(response.length()), 0);
            continue;
        }

        std::string response = executeCommand(tokens);
        send(client_socket, response.c_str(), static_cast<int>(response.length()), 0);
    }

    CLOSE_SOCKET(client_socket);
}

std::string Server::executeCommand(const std::vector<std::string> &tokens)
{
    if (tokens.empty())
        return RESP::encodeError("ERR empty command");

    std::string cmd = tokens[0];
    for (char &c : cmd)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    // Handle PING command
    if (cmd == "PING")
    {
        if (tokens.size() == 1)
            return RESP::encodeSimpleString("PONG");
        else
            return RESP::encodeBulkString(tokens[1]);
    }

    // Handle ECHO command
    else if (cmd == "ECHO" && tokens.size() >= 2)
    {
        return RESP::encodeBulkString(tokens[1]);
    }

    // Handle SET command
    else if (cmd == "SET" && tokens.size() >= 3)
    {
        std::string value;
        for (size_t i = 2; i < tokens.size(); i++)
        {
            if (i > 2) value += " ";
            value += tokens[i];
        }
        db_.set(tokens[1], value);
        return RESP::encodeSimpleString("OK");
    }

    // Handle GET command
    else if (cmd == "GET" && tokens.size() >= 2)
    {
        GetResult res = db_.get(tokens[1]);
        if (res.status == GetResult::NIL)
            return RESP::encodeNullBulkString();
        if (res.status == GetResult::WRONG_TYPE)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeBulkString(res.value);
    }

    // Handle DEL command
    else if (cmd == "DEL" && tokens.size() >= 2)
    {
        long long count = 0;
        for (size_t i = 1; i < tokens.size(); i++)
        {
            count += db_.del(tokens[i]);
        }
        return RESP::encodeInteger(count);
    }

    // Handle EXISTS command
    else if (cmd == "EXISTS" && tokens.size() >= 2)
    {
        long long count = 0;
        for (size_t i = 1; i < tokens.size(); i++)
        {
            if (db_.exists(tokens[i])) count++;
        }
        return RESP::encodeInteger(count);
    }

    // Handle INCR command
    else if (cmd == "INCR" && tokens.size() >= 2)
    {
        auto res = db_.incr(tokens[1]);
        if (!res.has_value())
            return RESP::encodeError("ERR WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(res.value());
    }

    // Handle INCRBY command
    else if (cmd == "INCRBY" && tokens.size() >= 3)
    {
        try {
            long long amount = std::stoll(tokens[2]);
            auto res = db_.incrby(tokens[1], amount);
            if (!res.has_value())
                return RESP::encodeError("ERR WRONGTYPE Operation against a key holding the wrong kind of value");
            return RESP::encodeInteger(res.value());
        } catch (...) {
            return RESP::encodeError("ERR value is not an integer or out of range");
        }
    }

    // Handle DECR command
    else if (cmd == "DECR" && tokens.size() >= 2)
    {
        auto res = db_.decr(tokens[1]);
        if (!res.has_value())
            return RESP::encodeError("ERR WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(res.value());
    }

    // Handle DECRBY command
    else if (cmd == "DECRBY" && tokens.size() >= 3)
    {
        try {
            long long amount = std::stoll(tokens[2]);
            auto res = db_.decrby(tokens[1], amount);
            if (!res.has_value())
                return RESP::encodeError("ERR WRONGTYPE Operation against a key holding the wrong kind of value");
            return RESP::encodeInteger(res.value());
        } catch (...) {
            return RESP::encodeError("ERR value is not an integer or out of range");
        }
    }

    // Handle EXPIRE command
    else if (cmd == "EXPIRE" && tokens.size() >= 3)
    {
        try {
            long long seconds = std::stoll(tokens[2]);
            bool success = db_.expire(tokens[1], seconds);
            return RESP::encodeInteger(success ? 1 : 0);
        } catch (...) {
            return RESP::encodeError("ERR value is not an integer or out of range");
        }
    }

    // Handle TTL command
    else if (cmd == "TTL" && tokens.size() >= 2)
    {
        long long ttl = db_.ttl(tokens[1]);
        return RESP::encodeInteger(ttl);
    }

    // Handle PERSIST command
    else if (cmd == "PERSIST" && tokens.size() >= 2)
    {
        bool success = db_.persist(tokens[1]);
        return RESP::encodeInteger(success ? 1 : 0);
    }

    // Handle TYPE command
    else if (cmd == "TYPE" && tokens.size() >= 2)
    {
        std::string t = db_.type(tokens[1]);
        return RESP::encodeSimpleString(t);
    }

    // Handle APPEND command
    else if (cmd == "APPEND" && tokens.size() >= 3)
    {
        long long newLen = db_.append(tokens[1], tokens[2]);
        if (newLen < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(newLen);
    }

    // Handle STRLEN command
    else if (cmd == "STRLEN" && tokens.size() >= 2)
    {
        long long len = db_.strlen(tokens[1]);
        if (len < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(len);
    }

    // ============== List Commands ==============

    // Handle LPUSH command
    else if (cmd == "LPUSH" && tokens.size() >= 3)
    {
        std::vector<std::string> values(tokens.begin() + 2, tokens.end());
        long long len = db_.lpush(tokens[1], values);
        if (len < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(len);
    }

    // Handle RPUSH command
    else if (cmd == "RPUSH" && tokens.size() >= 3)
    {
        std::vector<std::string> values(tokens.begin() + 2, tokens.end());
        long long len = db_.rpush(tokens[1], values);
        if (len < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(len);
    }

    // Handle LPOP command
    else if (cmd == "LPOP" && tokens.size() >= 2)
    {
        GetResult res = db_.lpop(tokens[1]);
        if (res.status == GetResult::NIL)
            return RESP::encodeNullBulkString();
        if (res.status == GetResult::WRONG_TYPE)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeBulkString(res.value);
    }

    // Handle RPOP command
    else if (cmd == "RPOP" && tokens.size() >= 2)
    {
        GetResult res = db_.rpop(tokens[1]);
        if (res.status == GetResult::NIL)
            return RESP::encodeNullBulkString();
        if (res.status == GetResult::WRONG_TYPE)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeBulkString(res.value);
    }

    // Handle LLEN command
    else if (cmd == "LLEN" && tokens.size() >= 2)
    {
        long long len = db_.llen(tokens[1]);
        if (len < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(len);
    }

    // Handle LRANGE command
    else if (cmd == "LRANGE" && tokens.size() >= 4)
    {
        try {
            long long start = std::stoll(tokens[2]);
            long long stop = std::stoll(tokens[3]);
            RangeResult res = db_.lrange(tokens[1], start, stop);
            if (res.wrong_type)
                return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
            return RESP::encodeArray(res.values);
        } catch (...) {
            return RESP::encodeError("ERR value is not an integer or out of range");
        }
    }

    // Handle LINDEX command
    else if (cmd == "LINDEX" && tokens.size() >= 3)
    {
        try {
            long long index = std::stoll(tokens[2]);
            GetResult res = db_.lindex(tokens[1], index);
            if (res.status == GetResult::NIL)
                return RESP::encodeNullBulkString();
            if (res.status == GetResult::WRONG_TYPE)
                return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
            return RESP::encodeBulkString(res.value);
        } catch (...) {
            return RESP::encodeError("ERR value is not an integer or out of range");
        }
    }

    // Handle LSET command
    else if (cmd == "LSET" && tokens.size() >= 4)
    {
        try {
            long long index = std::stoll(tokens[2]);
            LSetStatus status = db_.lset(tokens[1], index, tokens[3]);
            if (status == LSetStatus::WRONG_TYPE)
                return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
            if (status == LSetStatus::NO_SUCH_KEY)
                return RESP::encodeError("ERR no such key");
            if (status == LSetStatus::INDEX_OUT_OF_RANGE)
                return RESP::encodeError("ERR index out of range");
            return RESP::encodeSimpleString("OK");
        } catch (...) {
            return RESP::encodeError("ERR value is not an integer or out of range");
        }
    }

    // ============== Set Commands ==============

    // Handle SADD command
    else if (cmd == "SADD" && tokens.size() >= 3)
    {
        std::vector<std::string> members(tokens.begin() + 2, tokens.end());
        long long added = db_.sadd(tokens[1], members);
        if (added < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(added);
    }

    // Handle SREM command
    else if (cmd == "SREM" && tokens.size() >= 3)
    {
        long long removed = db_.srem(tokens[1], tokens[2]);
        if (removed < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(removed);
    }

    // Handle SMEMBERS command
    else if (cmd == "SMEMBERS" && tokens.size() >= 2)
    {
        RangeResult res = db_.smembers(tokens[1]);
        if (res.wrong_type)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeArray(res.values);
    }

    // Handle SISMEMBER command
    else if (cmd == "SISMEMBER" && tokens.size() >= 3)
    {
        int exists = db_.sismember(tokens[1], tokens[2]);
        if (exists < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(exists);
    }

    // Handle SCARD command
    else if (cmd == "SCARD" && tokens.size() >= 2)
    {
        long long size = db_.scard(tokens[1]);
        if (size < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(size);
    }

    // ============== Hash Commands ==============

    // Handle HSET command
    else if (cmd == "HSET" && tokens.size() >= 4)
    {
        int isNew = db_.hset(tokens[1], tokens[2], tokens[3]);
        if (isNew < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(isNew);
    }

    // Handle HGET command
    else if (cmd == "HGET" && tokens.size() >= 3)
    {
        GetResult res = db_.hget(tokens[1], tokens[2]);
        if (res.status == GetResult::NIL)
            return RESP::encodeNullBulkString();
        if (res.status == GetResult::WRONG_TYPE)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeBulkString(res.value);
    }

    // Handle HDEL command
    else if (cmd == "HDEL" && tokens.size() >= 3)
    {
        int deleted = db_.hdel(tokens[1], tokens[2]);
        if (deleted < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(deleted);
    }

    // Handle HGETALL command
    else if (cmd == "HGETALL" && tokens.size() >= 2)
    {
        HashGetAllResult res = db_.hgetall(tokens[1]);
        if (res.wrong_type)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");

        std::vector<std::string> result;
        for (const auto &p : res.entries)
        {
            result.push_back(p.first);
            result.push_back(p.second);
        }
        return RESP::encodeArray(result);
    }

    // Handle HKEYS command
    else if (cmd == "HKEYS" && tokens.size() >= 2)
    {
        RangeResult res = db_.hkeys(tokens[1]);
        if (res.wrong_type)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeArray(res.values);
    }

    // Handle HVALS command
    else if (cmd == "HVALS" && tokens.size() >= 2)
    {
        RangeResult res = db_.hvals(tokens[1]);
        if (res.wrong_type)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeArray(res.values);
    }

    // Handle HLEN command
    else if (cmd == "HLEN" && tokens.size() >= 2)
    {
        long long len = db_.hlen(tokens[1]);
        if (len < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(len);
    }

    // Handle HEXISTS command
    else if (cmd == "HEXISTS" && tokens.size() >= 3)
    {
        int exists = db_.hexists(tokens[1], tokens[2]);
        if (exists < 0)
            return RESP::encodeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RESP::encodeInteger(exists);
    }

    // Handle SAVE command
    else if (cmd == "SAVE")
    {
        bool ok = db_.saveRDB();
        db_.startNewAOF();
        return ok ? RESP::encodeSimpleString("OK") : RESP::encodeError("ERR failed to save RDB");
    }

    return RESP::encodeError("ERR unknown command '" + tokens[0] + "'");
}

void Server::stop()
{
    running_ = false;
    if (SOCKET_IS_VALID(server_socket_))
    {
        std::cout << "# Closing server socket..." << std::endl;
        CLOSE_SOCKET(server_socket_);
        server_socket_ = INVALID_SOCKET_HANDLE;
    }
    cleanup_network();
}
