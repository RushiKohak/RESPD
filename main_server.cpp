#include "db.h"
#include "server.h"
#include <iostream>
#include <csignal>

static Server *global_server = nullptr;

static void signalHandler(int signum)
{
    std::cout << "\n# Received signal " << signum << " (Ctrl+C)" << std::endl;
    std::cout << "# Shutting down server..." << std::endl;

    if (global_server)
    {
        global_server->stop();
    }

    exit(0);
}

int main(int argc, char *argv[])
{
    int port = 6379;

    if (argc > 1)
    {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid port argument, using 6379" << std::endl;
        }
    }

    std::cout << "# Starting Respd Server..." << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    Db db;
    Server server(db, port);
    global_server = &server;

    std::cout << "# Press Ctrl+C to stop the server" << std::endl;
    server.start();

    return 0;
}
