#include "mock_server.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace lambda_runtime::mock {

MockServer::MockServer(int port) : port_(port) {}

MockServer::~MockServer() { stop(); }

std::string MockServer::endpoint() const {
    return "127.0.0.1:" + std::to_string(port_);
}

void MockServer::start() {
    running_ = true;
    accept_thread_ = std::thread(&MockServer::accept_loop, this);
}

void MockServer::stop() {
    running_ = false;
    if (server_fd_ != -1) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }
    // Wake any thread blocked in route_next so it can exit cleanly.
    queue_cv_.notify_all();
    if (accept_thread_.joinable())
        accept_thread_.join();
}

// ── accept_loop ────────────────────────────────────────────────────────────────
// Runs on accept_thread_. Creates the server socket and loops accepting
// one TCP connection at a time, handing each off to a detached thread.
void MockServer::accept_loop() {
    // 1. Create a TCP socket.
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[mock] socket() failed: " << std::strerror(errno) << "\n";
        return;
    }

    // 2. SO_REUSEADDR lets us restart immediately without waiting for the OS
    //    to release the port (avoids "Address already in use" during dev).
    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Bind to all interfaces on port_.
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<std::uint16_t>(port_));
    if (::bind(server_fd_,
               reinterpret_cast<sockaddr *>(&addr),
               sizeof(addr)) < 0) {
        std::cerr << "[mock] bind() failed: " << std::strerror(errno) << "\n";
        return;
    }

    // 4. Start listening; backlog=10 means up to 10 pending connections.
    ::listen(server_fd_, 10);
    std::cout << "[mock] listening on " << endpoint() << "\n";

    while (running_) {
        int client_fd = ::accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (running_)
                std::cerr << "[mock] accept() error: " << std::strerror(errno) << "\n";
            break;
        }
        // Each connection lives on its own thread so GET /next can block
        // without preventing POST /invoke from being accepted.
        std::thread t([this, client_fd]() { handle_connection(client_fd); });
        t.detach();
    }
}

// ── read_request ───────────────────────────────────────────────────────────────
// Reads raw bytes until the HTTP header block ends (\r\n\r\n), then reads
// the body if Content-Length is present.
bool MockServer::read_request(int client_fd, HttpRequest &out) {
    std::string raw;
    char buf[4096];

    // Keep reading until we have the full header block.
    while (raw.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        raw.append(buf, static_cast<std::size_t>(n));
    }

    // First line: "METHOD /path HTTP/1.1"
    std::istringstream stream(raw);
    std::string version;
    stream >> out.method >> out.path >> version;

    // Find Content-Length (case-sensitive match is fine for our own curl client).
    std::size_t content_length = 0;
    auto cl = raw.find("Content-Length:");
    if (cl == std::string::npos) cl = raw.find("content-length:");
    if (cl != std::string::npos)
        content_length = static_cast<std::size_t>(std::stoul(raw.substr(cl + 15)));

    if (content_length == 0) return true;

    // We may have already slurped part of the body past the header boundary.
    auto header_end = raw.find("\r\n\r\n") + 4;
    out.body = raw.substr(header_end);

    while (out.body.size() < content_length) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        out.body.append(buf, static_cast<std::size_t>(n));
    }

    return true;
}

// ── handle_connection ──────────────────────────────────────────────────────────
// Parses the request and dispatches to the right route handler.
void MockServer::handle_connection(int client_fd) {
    HttpRequest req;
    if (!read_request(client_fd, req)) {
        ::close(client_fd);
        return;
    }

    // GET /*/invocation/next
    if (req.method == "GET" &&
        req.path.find("/invocation/next") != std::string::npos) {
        route_next(client_fd);
        return;
    }

    // POST /invoke  (our trigger endpoint)
    if (req.method == "POST" && req.path == "/invoke") {
        route_invoke(client_fd, req.body);
        return;
    }

    // POST /*/invocation/{id}/response
    if (req.method == "POST" &&
        req.path.find("/response") != std::string::npos) {
        auto inv = req.path.find("/invocation/");
        auto end = req.path.rfind("/response");
        route_response(client_fd,
                       req.path.substr(inv + 12, end - inv - 12),
                       req.body);
        return;
    }

    // POST /*/invocation/{id}/error
    if (req.method == "POST" &&
        req.path.find("/error") != std::string::npos) {
        auto inv = req.path.find("/invocation/");
        auto end = req.path.rfind("/error");
        route_error(client_fd,
                    req.path.substr(inv + 12, end - inv - 12),
                    req.body);
        return;
    }

    send_response(client_fd, 404, "text/plain", "Not Found");
    ::close(client_fd);
}

// ── route_next ─────────────────────────────────────────────────────────────────
// The runtime calls this and BLOCKS here until you POST to /invoke.
//
// TODO:
//   1. Acquire a std::unique_lock<std::mutex> on queue_mutex_.
//   2. Call queue_cv_.wait(lock, predicate) where the predicate returns true
//      when event_queue_ is non-empty OR !running_.
//      (This atomically releases the lock and suspends until notify_one/all.)
//   3. If !running_, send a 500 and close(client_fd), then return.
//   4. Pop the front event: auto event = event_queue_.front(); event_queue_.pop();
//   5. Release the lock (lock.unlock() or let it go out of scope).
//   6. Build the extra Lambda headers string:
//        "Lambda-Runtime-Aws-Request-Id: " + event.request_id + "\r\n"
//        "Lambda-Runtime-Deadline-Ms: "    + std::to_string(event.deadline_epoch_ms) + "\r\n"
//   7. send_response(client_fd, 200, "application/json", event.payload, extra_headers)
//   8. close(client_fd)
void MockServer::route_next(int client_fd) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this] { return !event_queue_.empty() || !running_; });

    if (!running_) {
        send_response(client_fd, 500, "text/plain", "server stopped");
        ::close(client_fd);
        return;
    }

    auto event = event_queue_.front();
    event_queue_.pop();
    lock.unlock();

    std::string extra =
        "Lambda-Runtime-Aws-Request-Id: " + event.request_id + "\r\n"
        "Lambda-Runtime-Deadline-Ms: " + std::to_string(event.deadline_epoch_ms) + "\r\n";

    send_response(client_fd, 200, "application/json", event.payload, extra);
    ::close(client_fd);
}

// ── route_invoke ───────────────────────────────────────────────────────────────
// You POST a JSON body here. The mock packages it as a PendingEvent and wakes
// the thread blocked in route_next.
//
// TODO:
//   1. Generate a request_id: make_request_id(request_counter_++)
//      (request_counter_ is only accessed here so no extra lock needed for it)
//   2. Compute deadline_epoch_ms = now + 15 minutes:
//        using namespace std::chrono;
//        auto now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
//        uint64_t deadline = static_cast<uint64_t>(now_ms) + 15 * 60 * 1000;
//   3. Lock queue_mutex_, push PendingEvent{request_id, body, deadline}, unlock.
//   4. queue_cv_.notify_one()  — wake the waiting route_next thread.
//   5. Build JSON ack: nlohmann::json j; j["requestId"] = request_id;
//   6. send_response(client_fd, 200, "application/json", j.dump())
//   7. close(client_fd)
void MockServer::route_invoke(int client_fd, const std::string &body) {
    std::string request_id = make_request_id(request_counter_++);

    using namespace std::chrono;
    auto now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    std::uint64_t deadline = static_cast<std::uint64_t>(now_ms) + 15ULL * 60 * 1000;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        event_queue_.push(PendingEvent{request_id, body, deadline});
    }
    queue_cv_.notify_one();

    nlohmann::json j;
    j["requestId"] = request_id;
    send_response(client_fd, 200, "application/json", j.dump());
    ::close(client_fd);
}

// ── route_response / route_error ───────────────────────────────────────────────
void MockServer::route_response(int client_fd, const std::string &request_id,
                                const std::string &body) {
    std::cout << "\n[mock] response for " << request_id << "\n"
              << body << "\n" << std::flush;
    send_response(client_fd, 202, "application/json", "{}");
    ::close(client_fd);
}

void MockServer::route_error(int client_fd, const std::string &request_id,
                             const std::string &body) {
    std::cout << "\n[mock] error for " << request_id << "\n"
              << body << "\n" << std::flush;
    send_response(client_fd, 202, "application/json", "{}");
    ::close(client_fd);
}

// ── send_response ──────────────────────────────────────────────────────────────
void MockServer::send_response(int fd, int status_code,
                               const std::string &content_type,
                               const std::string &body,
                               const std::string &extra_headers) {
    const char *status_text =
        status_code == 200 ? "OK" :
        status_code == 202 ? "Accepted" :
        status_code == 400 ? "Bad Request" :
        status_code == 404 ? "Not Found" :
                             "Internal Server Error";

    std::string response =
        "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n"
        "Content-Type: " + content_type + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n" +
        extra_headers +
        "\r\n" +
        body;

    ::send(fd, response.data(), response.size(), 0);
}

std::string MockServer::make_request_id(std::uint64_t counter) {
    return "mock-req-" + std::to_string(counter);
}

} // namespace lambda_runtime::mock
