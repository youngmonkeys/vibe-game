#pragma once

#include <cstddef>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace vibe::net::sockets {

#ifdef _WIN32
using Handle = SOCKET;
inline const Handle kNone = INVALID_SOCKET;
using IoResult = int;
#else
using Handle = int;
inline constexpr Handle kNone = -1;
using IoResult = long;
#endif

bool startup(std::string& reason);
Handle open(int family, int type, int protocol);
void close(Handle handle);
void shutdownBoth(Handle handle);
bool setBlocking(Handle handle, bool blocking);
bool setNoDelay(Handle handle);
int lastError();
bool interrupted(int error);
bool connectPending(int error);
std::string errorText(int error);
int connectResult(Handle handle);
int waitWritable(Handle handle, int timeoutMs);
int waitReadable(Handle handle, int timeoutMs);
IoResult send(Handle handle, const char* data, std::size_t size);
IoResult receive(Handle handle, char* data, std::size_t size);

} // namespace vibe::net::sockets
