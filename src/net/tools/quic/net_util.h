#ifndef NET_BASE_NET_UTIL_EXTRA_H_
#define NET_BASE_NET_UTIL_EXTRA_H_

#if defined(OS_WIN)
#include <windows.h>
#include <ws2tcpip.h>
#elif defined(OS_POSIX)
#include <sys/types.h>
#include <sys/socket.h>
#endif
#include <string.h>

namespace net {

// Convenience struct for when you need a |struct sockaddr|.
struct SockaddrStorage {
  SockaddrStorage() : addr_len(sizeof(addr_storage)),
                      addr(reinterpret_cast<struct sockaddr*>(&addr_storage)) {}

  SockaddrStorage(const SockaddrStorage& other)
    : addr_len(other.addr_len),
      addr(reinterpret_cast<struct sockaddr*>(&addr_storage)) {
    memcpy(addr, other.addr, addr_len);
  }

  void operator=(const SockaddrStorage& other) {
    addr_len = other.addr_len;
    // addr is already set to &this->addr_storage by default ctor.
    memcpy(addr, other.addr, addr_len);
  }

  struct sockaddr_storage addr_storage;
  socklen_t addr_len;
  struct sockaddr* const addr;
};

}

#endif // NET_BASE_NET_UTIL_EXTRA_H_
