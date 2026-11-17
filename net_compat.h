#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include "wepoll.h"

    typedef int socklen_t;
    typedef SOCKET socket_t;
    typedef HANDLE epoll_t;

    #define CLOSE_SOCKET closesocket
    #define SHUT_RDWR SD_BOTH
    #define INVALID_SOCKET_HANDLE INVALID_SOCKET
    #define SOCKET_IS_VALID(s) ((s) != INVALID_SOCKET)
    #define EPOLL_INVALID_HANDLE NULL
    #define epoll_close_handle epoll_close

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

    inline int epoll_ctl_add(epoll_t ep, socket_t sock, uint32_t events, void *ptr) {
        struct epoll_event ev;
        ev.events = events;
        ev.data.ptr = ptr;
        return epoll_ctl(ep, EPOLL_CTL_ADD, sock, &ev);
    }
    inline int epoll_ctl_mod(epoll_t ep, socket_t sock, uint32_t events, void *ptr) {
        struct epoll_event ev;
        ev.events = events;
        ev.data.ptr = ptr;
        return epoll_ctl(ep, EPOLL_CTL_MOD, sock, &ev);
    }
    inline int epoll_ctl_del(epoll_t ep, socket_t sock) {
        return epoll_ctl(ep, EPOLL_CTL_DEL, sock, nullptr);
    }

#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <sys/epoll.h>

    typedef int socket_t;
    typedef int epoll_t;

    #define CLOSE_SOCKET close
    #define INVALID_SOCKET_HANDLE (-1)
    #define SOCKET_IS_VALID(s) ((s) >= 0)
    #define EPOLL_INVALID_HANDLE (-1)
    #define epoll_close_handle close

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

    inline int epoll_ctl_add(epoll_t ep, socket_t sock, uint32_t events, void *ptr) {
        struct epoll_event ev;
        ev.events = events;
        ev.data.ptr = ptr;
        return epoll_ctl(ep, EPOLL_CTL_ADD, sock, &ev);
    }
    inline int epoll_ctl_mod(epoll_t ep, socket_t sock, uint32_t events, void *ptr) {
        struct epoll_event ev;
        ev.events = events;
        ev.data.ptr = ptr;
        return epoll_ctl(ep, EPOLL_CTL_MOD, sock, &ev);
    }
    inline int epoll_ctl_del(epoll_t ep, socket_t sock) {
        return epoll_ctl(ep, EPOLL_CTL_DEL, sock, nullptr);
    }
#endif

#endif
