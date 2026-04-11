#include "lambda_runtime/runtime.h"
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
  using namespace lambda_runtime;

  Handler handler = [](const Invocation &invocation) -> InvocationResponse {
    // Parse the incoming event
    auto event = nlohmann::json::parse(invocation.payload(), nullptr,
                                       /*allow_exceptions=*/false);
    if (event.is_discarded()) {
      return InvocationResponse::error("ParseError",
                                       "Event payload is not valid JSON");
    }

    // Read an optional "name" field, default to "world"
    std::string name = "world";
    if (event.contains("name") && event["name"].is_string()) {
      name = event["name"].get<std::string>();
    }

    // Build the response JSON
    nlohmann::json response;
    response["statusCode"] = 200;
    response["body"] = "Hello, " + name + "!";
    response["requestId"] = invocation.request_id();
    response["remainingMs"] = invocation.remaining_time().count();

    return InvocationResponse::success(response.dump());
  };

  // Start the runtime loop. Reads AWS_LAMBDA_RUNTIME_API from env.
  // Blocks here until a fatal transport error.
  std::cout << "[hello_world] starting runtime\n";
  Runtime runtime;
  runtime.run(handler);

  return 0;
}
