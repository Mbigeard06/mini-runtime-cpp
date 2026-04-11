#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace lambda_runtime::mock {

// A parsed HTTP request (just what we need).
struct HttpRequest {
    std::string method; // "GET" or "POST"
    std::string path;   // e.g. "/2018-06-01/runtime/invocation/next"
    std::string body;
};

// One event waiting to be picked up by the runtime via GET /next.
struct PendingEvent {
    std::string request_id;
    std::string payload;
    std::uint64_t deadline_epoch_ms;
};

// Minimal HTTP server that speaks the Lambda Runtime API protocol.
//
// Two endpoints from the Lambda spec:
//   GET  /2018-06-01/runtime/invocation/next          → long-poll, blocks until event
//   POST /2018-06-01/runtime/invocation/{id}/response → runtime posts result here
//   POST /2018-06-01/runtime/invocation/{id}/error    → runtime posts error here
//
// One extra endpoint for you to trigger invocations:
//   POST /invoke   body: any JSON   → queues an event, returns {"requestId": "..."}
class MockServer {
public:
    explicit MockServer(int port = 9001);
    ~MockServer();

    MockServer(const MockServer &) = delete;
    MockServer &operator=(const MockServer &) = delete;

    void start();
    void stop();

    // Returns the value to set as AWS_LAMBDA_RUNTIME_API.
    std::string endpoint() const;

private:
    int port_;
    int server_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    // Shared queue between route_invoke (producer) and route_next (consumer).
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<PendingEvent> event_queue_;
    std::uint64_t request_counter_{0};

    void accept_loop();
    void handle_connection(int client_fd);

    bool read_request(int client_fd, HttpRequest &out);

    // Lambda spec routes
    void route_next(int client_fd);
    void route_invoke(int client_fd, const std::string &body);
    void route_response(int client_fd, const std::string &request_id,
                        const std::string &body);
    void route_error(int client_fd, const std::string &request_id,
                     const std::string &body);

    static void send_response(int fd, int status_code,
                              const std::string &content_type,
                              const std::string &body,
                              const std::string &extra_headers = "");

    static std::string make_request_id(std::uint64_t counter);
};

} // namespace lambda_runtime::mock
