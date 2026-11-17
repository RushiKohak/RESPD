#ifndef SERVER_H
#define SERVER_H

#include "db.h"
#include "net_compat.h"
#include <string>
#include <thread>
#include <atomic>
#include <vector>

class Server
{
private:
    Db &db_;
    socket_t server_socket_;
    int port_;
    std::atomic<bool> running_;

    void handleClient(socket_t client_socket);
    std::string executeCommand(const std::vector<std::string> &tokens);

public:
    Server(Db &db, int port = 6379);
    ~Server();

    void start();
    void stop();
};

#endif
