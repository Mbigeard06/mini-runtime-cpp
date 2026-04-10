#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace lambda_runtime {

// ── Invocation
// snapshot of one Lambda invocation request.
class Invocation {
public:
  Invocation(std::string request_id, std::string payload,
             std::uint64_t deadline_epoch_ms,
             std::optional<std::string> invoked_function_arn,
             std::optional<std::string> client_context,
             std::optional<std::string> cognito_identity);

  // Unique ID for this invocation (UUID assigned by the Lambda service).
  [[nodiscard]] const std::string &request_id() const noexcept;

  // Raw JSON event body sent by the caller.
  [[nodiscard]] const std::string &payload() const noexcept;

  // Absolute deadline — milliseconds since Unix epoch.
  [[nodiscard]] std::uint64_t deadline_epoch_ms() const noexcept;

  // Optional fields forwarded from the Lambda service headers.
  [[nodiscard]] const std::optional<std::string> &
  invoked_function_arn() const noexcept;
  [[nodiscard]] const std::optional<std::string> &
  client_context() const noexcept;
  [[nodiscard]] const std::optional<std::string> &
  cognito_identity() const noexcept;

  // How much time is left before the deadline.
  [[nodiscard]] std::chrono::milliseconds remaining_time() const noexcept;

private:
  std::string request_id_;
  std::string payload_;
  std::uint64_t deadline_epoch_ms_;
  std::optional<std::string> invoked_function_arn_;
  std::optional<std::string> client_context_;
  std::optional<std::string> cognito_identity_;
};

struct InvocationResponse {
  std::string payload; // JSON body (success) or error envelope (failure)
  bool is_success;

  // Successful respons
  [[nodiscard]] static InvocationResponse success(std::string payload);

  // Error response
  // { "errorType": "...", "errorMessage": "..." }
  [[nodiscard]] static InvocationResponse error(std::string error_type,
                                                std::string error_message);
};

} // namespace lambda_runtime
