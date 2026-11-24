#include "vibe/net/transport/tcp/Sockets.h"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace vibe::net::sockets {

#ifdef _WIN32

bool startup(std::string& reason) {
    static const int result = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data);
    }();
    if (result != 0) {
        reason = "cannot start Winsock: " + errorText(result);
        return false;
    }
    return true;
}

Handle open(int family, int type, int protocol) {
    return ::socket(family, type, protocol);
}

void close(Handle handle) {
    if (handle != kNone) {
        ::closesocket(handle);
    }
}

void shutdownBoth(Handle handle) {
    if (handle != kNone) {
        ::shutdown(handle, SD_BOTH);
        CancelIoEx(reinterpret_cast<HANDLE>(handle), nullptr);
    }
}

bool setBlocking(Handle handle, bool blocking) {
    u_long mode = blocking ? 0 : 1;
    return ::ioctlsocket(handle, FIONBIO, &mode) == 0;
}

int lastError() {
    return WSAGetLastError();
}

bool interrupted(int error) {
    return error == WSAEINTR;
}

bool connectPending(int error) {
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

std::string errorText(int error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, static_cast<DWORD>(error), 0, reinterpret_cast<LPWSTR>(&buffer), 0,
                                        nullptr);
    std::string text;
    if (length > 0 && buffer) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr,
                                              nullptr);
        if (bytes > 0) {
            text.resize(static_cast<std::size_t>(bytes));
            WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), text.data(), bytes, nullptr, nullptr);
        }
    }
    if (buffer) {
        LocalFree(buffer);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.')) {
        text.pop_back();
    }
    return "error " + std::to_string(error) + (text.empty() ? std::string() : ": " + text);
}

int connectResult(Handle handle) {
    int error = 0;
    int length = sizeof error;
    if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0) {
        return lastError();
    }
    return error;
}

int waitWritable(Handle handle, int timeoutMs) {
    fd_set writable;
    fd_set failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(handle, &writable);
    FD_SET(handle, &failed);
    timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    return ::select(0, nullptr, &writable, &failed, &timeout);
}

int waitReadable(Handle handle, int timeoutMs) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(handle, &readable);
    timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    return ::select(0, &readable, nullptr, nullptr, &timeout);
}

IoResult send(Handle handle, const char* data, std::size_t size) {
    return ::send(handle, data, static_cast<int>(size), 0);
}

IoResult receive(Handle handle, char* data, std::size_t size) {
    return ::recv(handle, data, static_cast<int>(size), 0);
}

#else

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

bool startup(std::string&) {
    return true;
}

Handle open(int family, int type, int protocol) {
    const Handle handle = ::socket(family, type, protocol);
#ifdef SO_NOSIGPIPE
    if (handle >= 0) {
        const int noSigPipe = 1;
        ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof noSigPipe);
    }
#endif
    return handle;
}

void close(Handle handle) {
    if (handle >= 0) {
        ::close(handle);
    }
}

void shutdownBoth(Handle handle) {
    if (handle >= 0) {
        ::shutdown(handle, SHUT_RDWR);
    }
}

bool setBlocking(Handle handle, bool blocking) {
    const int flags = fcntl(handle, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(handle, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == 0;
}

int lastError() {
    return errno;
}

bool interrupted(int error) {
    return error == EINTR;
}

bool connectPending(int error) {
    return error == EINPROGRESS;
}

std::string errorText(int error) {
    return std::strerror(error);
}

int connectResult(Handle handle) {
    int error = 0;
    socklen_t length = sizeof error;
    if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, &error, &length) != 0) {
        return errno;
    }
    return error;
}

int waitWritable(Handle handle, int timeoutMs) {
    pollfd waiting{handle, POLLOUT, 0};
    return ::poll(&waiting, 1, timeoutMs);
}

int waitReadable(Handle handle, int timeoutMs) {
    pollfd waiting{handle, POLLIN, 0};
    return ::poll(&waiting, 1, timeoutMs);
}

IoResult send(Handle handle, const char* data, std::size_t size) {
    return ::send(handle, data, size, kSendFlags);
}

IoResult receive(Handle handle, char* data, std::size_t size) {
    return ::recv(handle, data, size, 0);
}

#endif

bool setNoDelay(Handle handle) {
    const int noDelay = 1;
    return ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay),
                        sizeof noDelay) == 0;
}

} // namespace vibe::net::sockets
