#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    typedef int socklen_t;
    typedef SOCKET socket_t;
    #define CLOSE_SOCKET closesocket
    #define SHUT_RDWR SD_BOTH
    #define INVALID_SOCKET_HANDLE INVALID_SOCKET
    #define SOCKET_IS_VALID(s) ((s) != INVALID_SOCKET)
    inline int set_nonblocking(socket_t fd) {
        u_long mode = 1;
        return ioctlsocket(fd, FIONBIO, &mode);
    }
    inline void init_network() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    inline void cleanup_network() {
        WSACleanup();
    }
    inline int get_last_socket_error() {
        return WSAGetLastError();
    }
    #define WOULD_BLOCK(err) ((err) == WSAEWOULDBLOCK)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    typedef int socket_t;
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET_HANDLE (-1)
    #define SOCKET_IS_VALID(s) ((s) >= 0)
    inline int set_nonblocking(socket_t fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    inline void init_network() {}
    inline void cleanup_network() {}
    inline int get_last_socket_error() {
        return errno;
    }
    #define WOULD_BLOCK(err) ((err) == EAGAIN || (err) == EWOULDBLOCK)
#endif

#endif
