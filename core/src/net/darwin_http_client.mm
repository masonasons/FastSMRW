#include "fastsm/net/darwin_http_client.hpp"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <string>
#include <string_view>

#include "fastsm/util/log.hpp"

// NSURLSession-backed IHttpClient for Apple platforms. TLS, proxies and HTTP/2
// are handled natively by the system. send() is synchronous (a semaphore waits
// on the completion handler); send_stream() bridges NSURLSession's delegate
// callbacks back onto the calling worker thread via a condition variable so
// on_chunk runs on that thread, exactly like WinHttpClient.

namespace {

NSString* ns(std::string_view s) {
    return [[NSString alloc] initWithBytes:s.data() length:(NSUInteger)s.size()
                                  encoding:NSUTF8StringEncoding];
}

std::string cpp(NSString* s) {
    if (!s)
        return {};
    const char* c = [s UTF8String];
    return c ? std::string(c) : std::string{};
}

NSMutableURLRequest* build_request(const fastsm::net::HttpRequest& req,
                                   const std::string& user_agent, NSTimeInterval timeout) {
    NSURL* url = [NSURL URLWithString:ns(req.url)];
    if (!url)
        return nil;
    NSMutableURLRequest* r = [NSMutableURLRequest requestWithURL:url
                                                     cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                 timeoutInterval:timeout];
    r.HTTPMethod = ns(req.method.empty() ? std::string("GET") : req.method);
    bool has_ua = false;
    for (const auto& [key, value] : req.headers) {
        [r setValue:ns(value) forHTTPHeaderField:ns(key)];
        std::string lower = key;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "user-agent")
            has_ua = true;
    }
    if (!has_ua && !user_agent.empty())
        [r setValue:ns(user_agent) forHTTPHeaderField:@"User-Agent"];
    if (!req.body.empty())
        r.HTTPBody = [NSData dataWithBytes:req.body.data() length:req.body.size()];
    return r;
}

void copy_response_headers(NSHTTPURLResponse* http, fastsm::net::Headers& out) {
    for (NSString* key in http.allHeaderFields) {
        id value = http.allHeaderFields[key];
        if ([value isKindOfClass:[NSString class]])
            out.emplace_back(cpp(key), cpp((NSString*)value));
    }
}

} // namespace

// Delegate that funnels streaming chunks to the blocked worker thread.
@interface FSMStreamDelegate : NSObject <NSURLSessionDataDelegate>
@property(atomic, assign) long status;
@property(atomic, assign) BOOL done;
@end

@implementation FSMStreamDelegate {
    NSCondition* _cond;
    NSMutableData* _buffer;
}

- (instancetype)init {
    if ((self = [super init])) {
        _cond = [[NSCondition alloc] init];
        _buffer = [NSMutableData data];
        _status = 0;
        _done = NO;
    }
    return self;
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    if ([response isKindOfClass:[NSHTTPURLResponse class]])
        self.status = (long)((NSHTTPURLResponse*)response).statusCode;
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveData:(NSData*)data {
    [_cond lock];
    [_buffer appendData:data];
    [_cond signal];
    [_cond unlock];
}

- (void)URLSession:(NSURLSession*)session
                    task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error {
    [_cond lock];
    self.done = YES;
    [_cond signal];
    [_cond unlock];
}

// Blocks until a chunk is available or the stream ends; drains the buffer into
// `out` and returns whether the stream is still open.
- (BOOL)waitForChunk:(NSMutableData*)out {
    [_cond lock];
    while (_buffer.length == 0 && !_done)
        [_cond wait];
    if (_buffer.length > 0) {
        [out appendData:_buffer];
        _buffer.length = 0;
    }
    BOOL open = !_done;
    [_cond unlock];
    return open;
}

// Wake a blocked waitForChunk without waiting for the network (used to break out
// promptly when the reader is being cancelled).
- (void)wake {
    [_cond lock];
    [_cond signal];
    [_cond unlock];
}

@end

namespace fastsm::net {

DarwinHttpClient::DarwinHttpClient(std::string user_agent) : user_agent_(std::move(user_agent)) {}

DarwinHttpClient::~DarwinHttpClient() { cancel_streams(); }

HttpResponse DarwinHttpClient::send(const HttpRequest& req) {
    // __block so the completion handler (run on another thread) can write it.
    __block HttpResponse res;
    @autoreleasepool {
        NSMutableURLRequest* request = build_request(req, user_agent_, 30.0);
        if (!request) {
            res.error = "invalid url";
            return res;
        }

        NSURLSessionConfiguration* cfg =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.timeoutIntervalForRequest = 30.0;
        cfg.timeoutIntervalForResource = 60.0;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:cfg];

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        NSURLSessionDataTask* task = [session
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                if (error) {
                    res.error = cpp(error.localizedDescription);
                    if (res.error.empty())
                        res.error = "network error";
                } else if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
                    NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
                    res.status = (long)http.statusCode;
                    copy_response_headers(http, res.headers);
                    if (data.length)
                        res.body.assign((const char*)data.bytes, data.length);
                }
                dispatch_semaphore_signal(sem);
              }];
        [task resume];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];
    }
    return res;
}

void DarwinHttpClient::send_stream(const HttpRequest& req,
                                   const std::function<bool()>& should_continue,
                                   const std::function<void(std::string_view)>& on_chunk) {
    @autoreleasepool {
        // Long idle timeout: Mastodon sends keep-alives every ~30s, so 60s lets a
        // genuinely dead connection drop; should_continue is re-checked each loop.
        NSMutableURLRequest* request = build_request(req, user_agent_, 60.0);
        if (!request)
            return;

        FSMStreamDelegate* delegate = [[FSMStreamDelegate alloc] init];
        NSOperationQueue* queue = [[NSOperationQueue alloc] init];
        queue.maxConcurrentOperationCount = 1;
        NSURLSessionConfiguration* cfg =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.timeoutIntervalForRequest = 60.0;
        cfg.timeoutIntervalForResource = 0; // no overall cap on a long-lived stream
        NSURLSession* session = [NSURLSession sessionWithConfiguration:cfg
                                                             delegate:delegate
                                                        delegateQueue:queue];
        NSURLSessionDataTask* task = [session dataTaskWithRequest:request];

        // Register so cancel_streams() can cancel this task and wake the reader.
        void* handle = (void*)CFBridgingRetain(task);
        {
            std::lock_guard<std::mutex> lk(stream_mutex_);
            active_streams_.push_back(handle);
        }

        [task resume];

        NSMutableData* chunk = [NSMutableData data];
        for (;;) {
            if (!should_continue())
                break;
            chunk.length = 0;
            const BOOL open = [delegate waitForChunk:chunk];
            const long status = delegate.status;
            if (chunk.length > 0 && status >= 200 && status < 300 && should_continue())
                on_chunk(std::string_view((const char*)chunk.bytes, chunk.length));
            if (!open)
                break; // stream ended (server closed, error, or cancelled)
        }

        [task cancel];
        [session invalidateAndCancel];

        // Deregister + release exactly once (cancel_streams may have raced us).
        std::lock_guard<std::mutex> lk(stream_mutex_);
        auto it = std::find(active_streams_.begin(), active_streams_.end(), handle);
        if (it != active_streams_.end()) {
            active_streams_.erase(it);
            CFBridgingRelease(handle);
        }
    }
}

void DarwinHttpClient::cancel_streams() {
    // Cancel under the lock: send_stream does its CFBridgingRelease (which frees
    // the task) under the same lock, so a handle we hold here is always still
    // valid. [cancel] is non-blocking, so holding the lock is cheap.
    std::lock_guard<std::mutex> lk(stream_mutex_);
    if (!active_streams_.empty())
        fastsm::log::write("darwin: cancel_streams cancelling " +
                           std::to_string(active_streams_.size()) + " active stream(s)");
    for (void* h : active_streams_) {
        NSURLSessionDataTask* task = (__bridge NSURLSessionDataTask*)h;
        [task cancel];
    }
}

} // namespace fastsm::net
