#ifndef SERVER_H
#define SERVER_H

#include "db.h"
#include "net_compat.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>

struct Connection
{
    socket_t fd = INVALID_SOCKET_HANDLE;
    std::string read_buffer;
    std::string write_buffer;
    uint32_t epoll_events = 0;
    bool is_server = false;
    bool close_after_write = false;
    bool is_replica = false;
};

class Server
{
protected:
    Db &db_;
    int port_;
    socket_t server_socket_;
    epoll_t epoll_;
    std::atomic<bool> running_;

    std::shared_ptr<Connection> server_conn_;
    std::unordered_map<socket_t, std::shared_ptr<Connection>> connections_;

    void handleAccept();
    void handleRead(Connection *conn);
    void handleWrite(Connection *conn);
    void processReadBuffer(Connection *conn);
    void closeConnection(socket_t fd);
    virtual void onTick();

public:
    Server(Db &db, int port = 6379);
    virtual ~Server();

    void start();
    void stop();
    std::string executeCommand(const std::vector<std::string> &tokens, Connection *conn = nullptr);
};

#endif
