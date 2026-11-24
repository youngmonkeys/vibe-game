#include "vibe/net/transport/HttpRequest.h"

#import <Foundation/Foundation.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <memory>

namespace vibe::net {
namespace {

struct Answer {
    HttpClient::Request* request = nullptr;
    int status = 0;
    std::string body;
};

NSURLSession* session() {
    static NSURLSession* shared = [] {
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        config.URLCache = nil;
        config.HTTPShouldSetCookies = NO;
        config.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
        config.timeoutIntervalForRequest = HttpClient::kTimeoutMs / 1000.0;
        config.timeoutIntervalForResource = HttpClient::kTimeoutMs / 1000.0;
        return [NSURLSession sessionWithConfiguration:config];
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

} // namespace

namespace http_backend {

void send(HttpClient::Request* request, const char* method) {
    NSString* text = [[NSString alloc] initWithBytes:request->url.data()
                                              length:request->url.size()
                                            encoding:NSUTF8StringEncoding];
    NSURL* url = text ? [NSURL URLWithString:text] : nil;
    if (!url) {
        post(new Answer{request, 0, {}});
        return;
    }
    NSMutableURLRequest* urlRequest = [NSMutableURLRequest requestWithURL:url];
    urlRequest.HTTPMethod = [NSString stringWithUTF8String:method];
    urlRequest.timeoutInterval = HttpClient::kTimeoutMs / 1000.0;
    for (std::size_t i = 0; i + 1 < request->headers.size(); i += 2) {
        NSString* name = [NSString stringWithUTF8String:request->headers[i].c_str()];
        NSString* value = [NSString stringWithUTF8String:request->headers[i + 1].c_str()];
        if (name && value) {
            [urlRequest setValue:value forHTTPHeaderField:name];
        }
    }
    if (!request->body.empty()) {
        urlRequest.HTTPBody = [NSData dataWithBytes:request->body.data() length:request->body.size()];
    }
    NSURLSessionDataTask* task =
        [session() dataTaskWithRequest:urlRequest
                     completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                       auto* answer = new Answer{request, 0, {}};
                       if (!error && [response isKindOfClass:[NSHTTPURLResponse class]]) {
                           answer->status = static_cast<int>(((NSHTTPURLResponse*)response).statusCode);
                       }
                       if (data.length > 0) {
                           answer->body.assign(static_cast<const char*>(data.bytes), data.length);
                       }
                       post(answer);
                     }];
    [task resume];
}

} // namespace http_backend
} // namespace vibe::net
