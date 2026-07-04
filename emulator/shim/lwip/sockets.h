/**
 * \file lwip/sockets.h (host shim)
 * \brief The lwIP socket API is the BSD API; on POSIX hosts the system
 *        sockets provide it directly, so plugin socket traffic uses the
 *        machine's real network (FR-035).
 *
 * On Windows/MinGW this additionally adapts the POSIX idioms the reused
 * host_api_socket.cpp relies on to Winsock: void* buffers (Winsock wants
 * char*), fcntl(O_NONBLOCK) -> ioctlsocket(FIONBIO), struct timeval
 * SO_RCVTIMEO/SO_SNDTIMEO -> DWORD milliseconds, close -> closesocket.
 * main() performs the one-time WSAStartup.
 */
#pragma once

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

/* Pull the CRT close/_close declarations in BEFORE the close macro below so
 * no later declaration gets mangled by it. */
#include <io.h>
#include <unistd.h>

#include <cstdint>

/* windows.h leaks an ERROR object-like macro (wingdi) that would destroy
 * the vendored ServiceState::ERROR enumerator; the build also defines
 * NOGDI/WIN32_LEAN_AND_MEAN, this is the belt to those braces. */
#ifdef ERROR
#undef ERROR
#endif

typedef long suseconds_t;

static inline int emu_closesocket(int fd)
{
    return closesocket(static_cast<SOCKET>(fd));
}

#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0004
#endif
#define F_GETFL 3
#define F_SETFL 4

/* fcntl emulation: only the O_NONBLOCK dance host_api_socket performs.
 * GETFL reports "no flags"; SETFL toggles FIONBIO from the requested bit. */
static inline int fcntl(int fd, int cmd, long arg = 0)
{
    if (cmd == F_GETFL) {
        return 0;
    }
    if (cmd == F_SETFL) {
        u_long nonblock = (arg & O_NONBLOCK) ? 1 : 0;
        return ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &nonblock) == 0
                   ? 0
                   : -1;
    }
    return -1;
}

/* void*-buffer overloads: C++ picks these over Winsock's char* versions,
 * so the reused POSIX-style call sites compile unchanged. */
static inline int send(int fd, const void* buf, size_t len, int flags)
{
    return ::send(static_cast<SOCKET>(fd), static_cast<const char*>(buf),
                  static_cast<int>(len), flags);
}

static inline int recv(int fd, void* buf, size_t len, int flags)
{
    return ::recv(static_cast<SOCKET>(fd), static_cast<char*>(buf),
                  static_cast<int>(len), flags);
}

static inline int getsockopt(int fd, int level, int optname, void* optval,
                             socklen_t* optlen)
{
    int len = optlen ? static_cast<int>(*optlen) : 0;
    const int rc = ::getsockopt(static_cast<SOCKET>(fd), level, optname,
                                static_cast<char*>(optval), &len);
    if (optlen) {
        *optlen = static_cast<socklen_t>(len);
    }
    return rc;
}

static inline int setsockopt(int fd, int level, int optname, const void* optval,
                             socklen_t optlen)
{
    if (level == SOL_SOCKET &&
        (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) &&
        optlen == sizeof(struct timeval)) {
        // Winsock expects the timeout as DWORD milliseconds, not timeval.
        const struct timeval* tv = static_cast<const struct timeval*>(optval);
        const DWORD ms = static_cast<DWORD>(tv->tv_sec * 1000 +
                                            tv->tv_usec / 1000);
        return ::setsockopt(static_cast<SOCKET>(fd), level, optname,
                            reinterpret_cast<const char*>(&ms), sizeof(ms));
    }
    return ::setsockopt(static_cast<SOCKET>(fd), level, optname,
                        static_cast<const char*>(optval),
                        static_cast<int>(optlen));
}

/* In this translation-unit scope `close` only ever means "close a socket". */
#define close(fd) emu_closesocket(fd)

#else /* POSIX hosts */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#endif
