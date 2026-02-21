#ifndef NET_COMPAT_H
#define NET_COMPAT_H

/*
  net_compat.h (header-only)
  Small portability layer for sockets across:
    - Windows (Winsock2)
    - POSIX (Linux/macOS)
    - X68000 (elf2x68k + libsocket, Human68k)

  Notes (X68000 / libsocket):
    - errno is not reliable (often EIO for any error)
    - setsockopt/getsockopt exist but are for driver-specific options only.
      Typical POSIX options like SO_BROADCAST / SO_REUSEADDR are not provided.
    - emulate non-blocking receive by checking SO_SOCKLENRECV via getsockopt.
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <errno.h>
  #if !defined(__human68k__)
    #include <fcntl.h>
  #endif
  typedef int sock_t;
#endif

#ifndef INVALID_SOCKET
  #define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
  #define SOCKET_ERROR (-1)
#endif

static inline int net_init(void) {
#if defined(_WIN32)
  WSADATA wsa;
  return (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ? 0 : -1;
#else
  return 0;
#endif
}

static inline void net_cleanup(void) {
#if defined(_WIN32)
  WSACleanup();
#endif
}

static inline void sock_close(sock_t s) {
#if defined(_WIN32)
  closesocket(s);
#else
  close(s);
#endif
}

static inline int sock_set_nonblock(sock_t s) {
#if defined(_WIN32)
  u_long mode = 1;
  return (ioctlsocket(s, FIONBIO, &mode) == 0) ? 0 : -1;
#else
  #if defined(__human68k__)
    (void)s;
    return 0; /* prefer SO_SOCKLENRECV polling */
  #else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) {
      return -1;
    }
    if (fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
      return -1;
    }
    return 0;
  #endif
#endif
}

/*
  Non-blocking UDP receive helper.

  Returns:
    >0 : bytes received
     0 : no data available (would block)
    -1 : error
*/
static inline int udp_recvfrom_nb(sock_t s,
                                  void *buf,
                                  int buf_size,
                                  struct sockaddr_in *from,
                                  int *from_len) {
  if (!buf || buf_size <= 0 || !from || !from_len) {
    return -1;
  }

#if defined(__human68k__)
  /* X68000 / Human68k / elf2x68k libsocket:
     emulate non-blocking receive by checking SO_SOCKLENRECV. */
  {
    int avail = 0;
    socklen_t optlen = (socklen_t)sizeof(avail);
    if (getsockopt(s, 0, SO_SOCKLENRECV, &avail, &optlen) != 0) {
      return 0;
    }
    if (avail <= 0) {
      return 0;
    }
  }
  {
    socklen_t sl = (socklen_t)(*from_len);
    int n = (int)recvfrom(s, buf, (size_t)buf_size, 0,
                          (struct sockaddr *)from, &sl);
    if (n <= 0) {
      return -1;
    }
    *from_len = (int)sl;
    return n;
  }
#else
  {
    socklen_t sl = (socklen_t)(*from_len);
    int n = (int)recvfrom(s, buf, (size_t)buf_size, 0,
                          (struct sockaddr *)from, &sl);
    if (n < 0) {
#if defined(_WIN32)
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
        return 0;
      }
      return -1;
#else
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return 0;
      }
      return -1;
#endif
    }
    if (n == 0) {
      return 0;
    }
    *from_len = (int)sl;
    return n;
  }
#endif
}

static inline const char *net_last_error_str(void) {
#if defined(_WIN32)
  static char buf[64];
  int err = WSAGetLastError();
  snprintf(buf, sizeof(buf), "winsock error %d", err);
  return buf;
#else
  return strerror(errno);
#endif
}

#endif /* NET_COMPAT_H */
