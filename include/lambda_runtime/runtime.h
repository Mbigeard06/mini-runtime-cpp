#pragma once

#include "http_client.h"
#include "invocation.h"
#include <functional>
#include <memory>
#include <string>

namespace lambda_runtime {

// A handler takes one invocation and returns one response.
using Handler = std::function<InvocationResponse(const Invocation &)>;

// Implements the Lambda Runtime API protocol.
// Polls for events, calls the handler, posts the result. Loops until fatal
// error.
class Runtime {
public:
  // Reads AWS_LAMBDA_RUNTIME_API from environment. Throws if not set.
  explicit Runtime(std::unique_ptr<HttpClient> http_client = nullptr);

  // Explicit endpoint for testing with an injectable http client.
  Runtime(std::string endpoint, std::unique_ptr<HttpClient> http_client);

  ~Runtime() = default;

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;
  Runtime(Runtime &&) = default;
  Runtime &operator=(Runtime &&) = default;

  // Enters the event loop. Blocks until a fatal error.
  // All handler exceptions are caught and sent as invocation errors.
  void run(Handler handler);

private:
  static constexpr std::string_view kApiVersion = "2018-06-01";

  std::string endpoint_;
  std::unique_ptr<HttpClient> http_client_;

  [[nodiscard]] std::string next_invocation_url() const;
  [[nodiscard]] std::string response_url(const std::string &request_id) const;
  [[nodiscard]] std::string error_url(const std::string &request_id) const;
  [[nodiscard]] std::string init_error_url() const;

  [[nodiscard]] Outcome<Invocation> poll_next_invocation();

  [[nodiscard]] Outcome<void> post_response(const std::string &request_id,
                                            const std::string &payload);

  [[nodiscard]] Outcome<void>
  post_invocation_error(const std::string &request_id,
                        const std::string &error_json);

  [[nodiscard]] Outcome<void> post_init_error(const std::string &error_json);

  [[nodiscard]] static std::string
  make_error_json(const std::string &error_type,
                  const std::string &error_message);
};

} // namespace lambda_runtime
