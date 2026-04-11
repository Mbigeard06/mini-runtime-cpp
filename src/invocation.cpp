#include "lambda_runtime/invocation.h"
#include <nlohmann/json.hpp>

namespace lambda_runtime {

Invocation::Invocation(std::string request_id, std::string payload,
                       std::uint64_t deadline_epoch_ms,
                       std::optional<std::string> invoked_function_arn,
                       std::optional<std::string> client_context,
                       std::optional<std::string> cognito_identity)
    : request_id_{std::move(request_id)}, payload_{std::move(payload)},
      deadline_epoch_ms_{deadline_epoch_ms},
      invoked_function_arn_{std::move(invoked_function_arn)},
      client_context_{std::move(client_context)},
      cognito_identity_{std::move(cognito_identity)} {}

const std::string &Invocation::request_id() const noexcept {
  return request_id_;
}

const std::string &Invocation::payload() const noexcept { return payload_; }

std::uint64_t Invocation::deadline_epoch_ms() const noexcept {
  return deadline_epoch_ms_;
}

const std::optional<std::string> &
Invocation::invoked_function_arn() const noexcept {
  return invoked_function_arn_;
}

const std::optional<std::string> &Invocation::client_context() const noexcept {
  return client_context_;
}

const std::optional<std::string> &
Invocation::cognito_identity() const noexcept {
  return cognito_identity_;
}

std::chrono::milliseconds Invocation::remaining_time() const noexcept {
  using namespace std::chrono;

  const auto now_ms =
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count();

  const auto remaining = static_cast<std::int64_t>(deadline_epoch_ms_) -
                         static_cast<std::int64_t>(now_ms);

  // Floor at zero — never return a negative duration.
  return milliseconds{std::max<std::int64_t>(0, remaining)};
}

InvocationResponse InvocationResponse::success(std::string payload) {
  return InvocationResponse{std::move(payload), true};
}

InvocationResponse InvocationResponse::error(std::string error_type,
                                             std::string error_message) {
  nlohmann::json j;
  j["errorType"] = std::move(error_type);
  j["errorMessage"] = std::move(error_message);
  return InvocationResponse{j.dump(), false};
}

} // namespace lambda_runtime
