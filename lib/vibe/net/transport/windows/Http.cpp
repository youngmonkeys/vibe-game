#include "vibe/net/transport/HttpRequest.h"

#include <windows.h>
#include <winhttp.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_thread.h>

#include <algorithm>
#include <memory>
#include <string>

#include "vibe/core/AppInfo.h"
#include "vibe/net/transport/windows/Text.h"

namespace vibe::net {
namespace {

struct Answer {
    HttpClient::Request* request = nullptr;
    int status = 0;
    std::string body;
};

struct Job {
    HttpClient::Request* request = nullptr;
    std::wstring method;
};

struct InternetHandle {
    HINTERNET handle = nullptr;
    explicit InternetHandle(HINTERNET value) : handle(value) {}
    ~InternetHandle() {
        if (handle) {
            WinHttpCloseHandle(handle);
        }
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};

HINTERNET session() {
    static HINTERNET shared = [] {
        const std::wstring agent = windows_text::wide(app_info::kName);
        HINTERNET opened = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);
        if (opened) {
            WinHttpSetTimeouts(opened, HttpClient::kTimeoutMs, HttpClient::kTimeoutMs, HttpClient::kTimeoutMs,
                               HttpClient::kTimeoutMs);
        }
        return opened;
    }();
    return shared;
}

void SDLCALL completeOnMainThread(void* userdata) {
    std::unique_ptr<Answer> answer(static_cast<Answer*>(userdata));
    completeRequest(answer->request, answer->status, answer->body.data(), answer->body.size());
    std::fill(answer->body.begin(), answer->body.end(), '\0');
}

void post(Answer* answer) {
    if (!SDL_RunOnMainThread(completeOnMainThread, answer, false)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "http: cannot hand an answer to the main thread: %s",
                     SDL_GetError());
        std::fill(answer->body.begin(), answer->body.end(), '\0');
        delete answer;
    }
}

void logFailure(const char* what) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "http: %s failed: %s", what,
                windows_text::errorText(GetLastError()).c_str());
}

void perform(const Job& job, Answer& answer) {
    HttpClient::Request& request = *job.request;
    const std::wstring url = windows_text::wide(request.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (url.empty() || !WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        logFailure("WinHttpCrackUrl");
        return;
    }
    if (!session()) {
        logFailure("WinHttpOpen");
        return;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) {
        path = L"/";
    }
    InternetHandle connection(WinHttpConnect(session(), host.c_str(), parts.nPort, 0));
    if (!connection.handle) {
        logFailure("WinHttpConnect");
        return;
    }
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle handle(WinHttpOpenRequest(connection.handle, job.method.c_str(), path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!handle.handle) {
        logFailure("WinHttpOpenRequest");
        return;
    }
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    WinHttpSetOption(handle.handle, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof disabled);
    std::wstring headers;
    for (std::size_t i = 0; i + 1 < request.headers.size(); i += 2) {
        headers += windows_text::wide(request.headers[i]);
        headers += L": ";
        headers += windows_text::wide(request.headers[i + 1]);
        headers += L"\r\n";
    }
    LPVOID body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : request.body.data();
    const DWORD bodySize = static_cast<DWORD>(request.body.size());
    const bool sent = WinHttpSendRequest(handle.handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                         headers.empty() ? 0 : static_cast<DWORD>(-1L), body, bodySize, bodySize, 0);
    std::fill(headers.begin(), headers.end(), L'\0');
    if (!sent) {
        logFailure("WinHttpSendRequest");
        return;
    }
    if (!WinHttpReceiveResponse(handle.handle, nullptr)) {
        logFailure("WinHttpReceiveResponse");
        return;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof status;
    if (!WinHttpQueryHeaders(handle.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        logFailure("WinHttpQueryHeaders");
        return;
    }
    char buffer[16 * 1024];
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(handle.handle, buffer, sizeof buffer, &read)) {
            logFailure("WinHttpReadData");
            std::fill(answer.body.begin(), answer.body.end(), '\0');
            answer.body.clear();
            std::fill(std::begin(buffer), std::end(buffer), '\0');
            return;
        }
        if (read == 0) {
            break;
        }
        answer.body.append(buffer, read);
    }
    std::fill(std::begin(buffer), std::end(buffer), '\0');
    answer.status = static_cast<int>(status);
}

int SDLCALL run(void* userdata) {
    std::unique_ptr<Job> job(static_cast<Job*>(userdata));
    auto* answer = new Answer{job->request, 0, {}};
    perform(*job, *answer);
    post(answer);
    return 0;
}

} // namespace

namespace http_backend {

void send(HttpClient::Request* request, const char* method) {
    auto* job = new Job{request, windows_text::wide(method)};
    SDL_Thread* thread = SDL_CreateThread(run, "vibe-http", job);
    if (!thread) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "http: cannot start the HTTP thread: %s", SDL_GetError());
        delete job;
        post(new Answer{request, 0, {}});
        return;
    }
    SDL_DetachThread(thread);
}

} // namespace http_backend
} // namespace vibe::net
