# mini-lambda-runtime

A minimal C++17 implementation of the [AWS Lambda Runtime API](https://docs.aws.amazon.com/lambda/latest/dg/runtimes-api.html).

---

## What it is

AWS Lambda executes your code inside a managed container. The container exposes a small HTTP API that every Lambda runtime must speak:

```
GET  /2018-06-01/runtime/invocation/next               ← long-poll, blocks until an event arrives
POST /2018-06-01/runtime/invocation/{id}/response      ← success reply
POST /2018-06-01/runtime/invocation/{id}/error         ← error reply
```

This library handles that protocol loop so you can focus on writing handlers.

---

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  Your handler  (examples/hello_world.cpp)                      │
│  InvocationResponse handler(const Invocation &inv) { ... }    │
└────────────────────────┬───────────────────────────────────────┘
                         │  called by
┌────────────────────────▼───────────────────────────────────────┐
│  Runtime  (src/runtime.cpp)                                    │
│  while(true) {                                                 │
│    inv  = poll_next_invocation()  // GET /next                 │
│    resp = handler(inv)            // your code                 │
│    post_response or post_error    // POST back                 │
│  }                                                             │
└────────────────────────┬───────────────────────────────────────┘
                         │  uses
┌────────────────────────▼───────────────────────────────────────┐
│  HttpClient  (include/lambda_runtime/http_client.h)            │
│  ┌─────────────────────┐   ┌──────────────────────────────┐   │
│  │  CurlHttpClient     │   │  MockHttpClient  (tests only) │   │
│  │  (libcurl, prod)    │   │  (GMock, no network)          │   │
│  └─────────────────────┘   └──────────────────────────────┘   │
└────────────────────────────────────────────────────────────────┘
```

### Key types

| Type | File | Purpose |
|---|---|---|
| `Outcome<T>` | `include/lambda_runtime/outcome.h` | Type-safe success/error without exceptions |
| `Invocation` | `include/lambda_runtime/invocation.h` | One incoming Lambda event |
| `InvocationResponse` | `include/lambda_runtime/invocation.h` | What your handler returns |
| `HttpClient` | `include/lambda_runtime/http_client.h` | Abstract HTTP — swap in a mock for tests |
| `Runtime` | `include/lambda_runtime/runtime.h` | The event loop |

---

## Build

### Requirements

- CMake >= 3.20
- A C++17 compiler (clang or GCC)
- libcurl

```bash
# Configure
cmake -S . -B build

# Build everything (library + tests + examples + mock server)
cmake --build build --parallel

# Run tests
ctest --test-dir build --output-on-failure
```

CMake options (all `ON` by default):

| Option | What it builds |
|---|---|
| `BUILD_TESTS` | GoogleTest suite (20 tests) |
| `BUILD_EXAMPLES` | `hello_world` binary |
| `BUILD_MOCK` | Standalone mock Lambda server |

---

## Quick demo

Run the mock server and example handler side-by-side to see the full round-trip without touching AWS.

**Terminal 1 — mock server** (listens on port 9001):
```bash
./build/mock/mock_lambda_server
```

**Terminal 2 — runtime handler** (connects to the mock):
```bash
AWS_LAMBDA_RUNTIME_API=127.0.0.1:9001 ./build/examples/hello_world
```

**Terminal 3 — trigger an invocation**:
```bash
curl -s -X POST http://127.0.0.1:9001/invoke \
     -H "Content-Type: application/json" \
     -d '{"name": "Lambda"}'
```

The mock prints the response:
```
[mock] response for mock-req-0
{"message":"Hello, Lambda!"}
```

---

## Writing a handler

```cpp
#include "lambda_runtime/runtime.h"
#include "lambda_runtime/invocation.h"
#include <nlohmann/json.hpp>

int main() {
    lambda_runtime::Runtime rt;   // reads AWS_LAMBDA_RUNTIME_API from env

    rt.run([](const lambda_runtime::Invocation &inv)
               -> lambda_runtime::InvocationResponse {
        auto payload = nlohmann::json::parse(inv.payload());
        std::string name = payload.value("name", "world");

        nlohmann::json result;
        result["message"] = "Hello, " + name + "!";
        return lambda_runtime::InvocationResponse::success(result.dump());
    });
}
```

Exceptions thrown from the handler are automatically caught and forwarded to Lambda as `HandlerException` errors — the runtime never crashes.

---

## Tests

20 unit tests across three suites:

| Suite | Coverage |
|---|---|
| `OutcomeInt` / `OutcomeVoid` | `Outcome<T>` success/failure paths, move semantics, throw-on-bad-access |
| `Invocation` / `InvocationResponse` | Field storage, optional fields, `remaining_time()` boundary cases |
| `Runtime` | Handler called correctly, exceptions become errors, network failures exit cleanly — all with a GMock `HttpClient`, no real HTTP |

```
100% tests passed, 0 tests failed out of 20   (0.13 s)
```
