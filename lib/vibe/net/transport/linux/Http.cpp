#include "vibe/net/transport/HttpRequest.h"

#include <curl/curl.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_thread.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "vibe/core/AppInfo.h"

namespace vibe::net {
namespace {

struct Answer {
    HttpClient::Request* request = nullptr;
    int status = 0;
    std::string body;
};

struct Job {
    HttpClient::Request* request = nullptr;
    std::string method;
};

struct EasyHandle {
    CURL* handle = curl_easy_init();
    EasyHandle() = default;
    ~EasyHandle() {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }
    EasyHandle(const EasyHandle&) = delete;
    EasyHandle& operator=(const EasyHandle&) = delete;
};

struct HeaderList {
    curl_slist* list = nullptr;
    std::string scratch;
    HeaderList() = default;
    ~HeaderList() {
        for (curl_slist* item = list; item; item = item->next) {
            std::fill(item->data, item->data + std::strlen(item->data), '\0');
        }
        curl_slist_free_all(list);
        std::fill(scratch.begin(), scratch.end(), '\0');
    }
    HeaderList(const HeaderList&) = delete;
    HeaderList& operator=(const HeaderList&) = delete;
    bool add(const std::string& line) {
        curl_slist* next = curl_slist_append(list, line.c_str());
        if (!next) {
            return false;
        }
        list = next;
        return true;
    }
};

bool globalReady() {
    static const bool ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return ready;
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

std::size_t onData(char* data, std::size_t size, std::size_t count, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(data, size * count);
    return size * count;
}

void perform(const Job& job, Answer& answer) {
    HttpClient::Request& request = *job.request;
    EasyHandle easy;
    if (!easy.handle) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "http: curl_easy_init failed");
        return;
    }
    HeaderList headers;
    for (std::size_t i = 0; i + 1 < request.headers.size(); i += 2) {
        headers.scratch = request.headers[i] + ": " + request.headers[i + 1];
        if (!headers.add(headers.scratch)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "http: cannot build the request headers");
            return;
        }
    }
    if (!headers.add("Expect:")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "http: cannot build the request headers");
        return;
    }
    char errorBuffer[CURL_ERROR_SIZE] = {};
    CURL* curl = easy.handle;
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(HttpClient::kTimeoutMs));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(HttpClient::kTimeoutMs));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, app_info::kName);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.list);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer.body);
    if (job.method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else {
        if (job.method != "POST") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, job.method.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
    }
    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "http: request failed: %s",
                    errorBuffer[0] ? errorBuffer : curl_easy_strerror(result));
        std::fill(answer.body.begin(), answer.body.end(), '\0');
        answer.body.clear();
        return;
    }
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
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
    if (!globalReady()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "http: curl_global_init failed");
        post(new Answer{request, 0, {}});
        return;
    }
    auto* job = new Job{request, method};
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
