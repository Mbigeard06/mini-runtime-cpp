#include "lambda_runtime/runtime.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace lambda_runtime {

namespace {

std::string get_env_or_throw(const char *name) {
  const char *value = std::getenv(name);
  if (!value) {
    throw std::runtime_error(
        std::string("Required environment variable not set: ") + name);
  }
  return value;
}

std::optional<std::string>
get_header(const std::unordered_map<std::string, std::string> &headers,
           const std::string &key) {
  auto it = headers.find(key);
  if (it == headers.end())
    return std::nullopt;
  return it->second;
}

std::uint64_t
parse_deadline_ms(const std::unordered_map<std::string, std::string> &headers) {
  auto val = get_header(headers, "lambda-runtime-deadline-ms");
  if (!val) {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch())
            .count() +
        15 * 60 * 1000);
  }
  return std::stoull(*val);
}

} // namespace

// ── Constructors
// ───────────────────────────────────────────────────────────────

Runtime::Runtime(std::unique_ptr<HttpClient> http_client)
    : endpoint_(get_env_or_throw("AWS_LAMBDA_RUNTIME_API")),
      http_client_(http_client ? std::move(http_client)
                               : std::make_unique<CurlHttpClient>()) {}

Runtime::Runtime(std::string endpoint, std::unique_ptr<HttpClient> http_client)
    : endpoint_(std::move(endpoint)),
      http_client_(http_client ? std::move(http_client)
                               : std::make_unique<CurlHttpClient>()) {}

// ── URL builders
// ───────────────────────────────────────────────────────────────

std::string Runtime::next_invocation_url() const {
  return "http://" + endpoint_ + "/" + std::string(kApiVersion) +
         "/runtime/invocation/next";
}

std::string Runtime::response_url(const std::string &request_id) const {
  return "http://" + endpoint_ + "/" + std::string(kApiVersion) +
         "/runtime/invocation/" + request_id + "/response";
}

std::string Runtime::error_url(const std::string &request_id) const {
  return "http://" + endpoint_ + "/" + std::string(kApiVersion) +
         "/runtime/invocation/" + request_id + "/error";
}

std::string Runtime::init_error_url() const {
  return "http://" + endpoint_ + "/" + std::string(kApiVersion) +
         "/runtime/init/error";
}

// ── Runtime API calls
// ──────────────────────────────────────────────────────────

Outcome<Invocation> Runtime::poll_next_invocation() {
  auto result = http_client_->get(next_invocation_url());
  if (result.is_failure()) {
    return Outcome<Invocation>::failure(result.error().error_type,
                                        result.error().error_message);
  }

  const auto &response = result.value();
  const auto &headers = response.headers;

  auto request_id = get_header(headers, "lambda-runtime-aws-request-id");
  if (!request_id) {
    return Outcome<Invocation>::failure(
        "MissingHeader", "Missing Lambda-Runtime-Aws-Request-Id header");
  }

  return Outcome<Invocation>::success(
      Invocation{*request_id, response.body, parse_deadline_ms(headers),
                 get_header(headers, "lambda-runtime-invoked-function-arn"),
                 get_header(headers, "lambda-runtime-client-context"),
                 get_header(headers, "lambda-runtime-cognito-identity")});
}

Outcome<void> Runtime::post_response(const std::string &request_id,
                                     const std::string &payload) {
  auto result =
      http_client_->post(response_url(request_id), payload, "application/json");
  if (result.is_failure()) {
    return Outcome<void>::failure(result.error().error_type,
                                  result.error().error_message);
  }
  return Outcome<void>::success();
}

Outcome<void> Runtime::post_invocation_error(const std::string &request_id,
                                             const std::string &error_json) {
  auto result =
      http_client_->post(error_url(request_id), error_json, "application/json");
  if (result.is_failure()) {
    return Outcome<void>::failure(result.error().error_type,
                                  result.error().error_message);
  }
  return Outcome<void>::success();
}

Outcome<void> Runtime::post_init_error(const std::string &error_json) {
  auto result =
      http_client_->post(init_error_url(), error_json, "application/json");
  if (result.is_failure()) {
    return Outcome<void>::failure(result.error().error_type,
                                  result.error().error_message);
  }
  return Outcome<void>::success();
}

std::string Runtime::make_error_json(const std::string &error_type,
                                     const std::string &error_message) {
  nlohmann::json j;
  j["errorType"] = error_type;
  j["errorMessage"] = error_message;
  return j.dump();
}

// Runtime Loop

void Runtime::run(Handler handler) {
  while (true) {
    // 1. block until Lambda delivers the next event
    auto invocation_result = poll_next_invocation();
    if (invocation_result.is_failure()) {
      std::cerr << "[runtime] fatal: "
                << invocation_result.error().error_message << "\n";
      return;
    }

    const Invocation &invocation = invocation_result.value();

    // 2. call handler — catch everything so the runtime never crashes
    InvocationResponse response = [&]() {
      try {
        return handler(invocation);
      } catch (const std::exception &e) {
        return InvocationResponse::error("HandlerException", e.what());
      } catch (...) {
        return InvocationResponse::error("UnknownException",
                                         "handler threw an unknown exception");
      }
    }();

    // 3. post result back to Lambda service
    if (response.is_success) {
      auto post = post_response(invocation.request_id(), response.payload);
      if (post.is_failure()) {
        std::cerr << "[runtime] fatal: could not post response: "
                  << post.error().error_message << "\n";
        return;
      }
    } else {
      auto post =
          post_invocation_error(invocation.request_id(), response.payload);
      if (post.is_failure()) {
        std::cerr << "[runtime] fatal: could not post error: "
                  << post.error().error_message << "\n";
        return;
      }
    }
  }
}

} // namespace lambda_runtime
