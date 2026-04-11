#include "lambda_runtime/http_client.h"
#include "lambda_runtime/runtime.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace lambda_runtime;
using ::testing::Return;

// ── MockHttpClient ─────────────────────────────────────────────────────────────
// Replaces libcurl so tests run without a real network.

class MockHttpClient : public HttpClient {
public:
    MOCK_METHOD(Outcome<HttpResponse>, get,  (const std::string &url), (override));
    MOCK_METHOD(Outcome<HttpResponse>, post, (const std::string &url,
                                              const std::string &body,
                                              const std::string &content_type), (override));
};

// Helper: build the HTTP response that GET /next returns.
static Outcome<HttpResponse> make_next_response(const std::string &request_id,
                                                 const std::string &payload) {
    HttpResponse r;
    r.status_code = 200;
    r.body        = payload;
    r.headers["lambda-runtime-aws-request-id"] = request_id;
    // deadline = now + 15 min (milliseconds)
    using namespace std::chrono;
    auto now_ms = duration_cast<milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    r.headers["lambda-runtime-deadline-ms"] =
        std::to_string(static_cast<std::uint64_t>(now_ms) + 15ULL * 60 * 1000);
    return Outcome<HttpResponse>::success(std::move(r));
}

static Outcome<HttpResponse> make_202() {
    HttpResponse r;
    r.status_code = 202;
    r.body        = "{}";
    return Outcome<HttpResponse>::success(std::move(r));
}

// ── Tests ──────────────────────────────────────────────────────────────────────

// run() calls the handler once then exits when GET /next returns a failure on
// the second iteration (simulates the runtime service going away).
TEST(Runtime, CallsHandlerOnce) {
    auto *mock = new MockHttpClient;  // ownership moves into Runtime below

    // First GET /next: deliver one event.
    EXPECT_CALL(*mock, get(::testing::HasSubstr("/invocation/next")))
        .WillOnce(Return(make_next_response("req-1", R"({"hello":"world"})")))
        .WillOnce(Return(Outcome<HttpResponse>::failure("NetworkError", "gone")));

    // POST /response for that event.
    EXPECT_CALL(*mock, post(::testing::HasSubstr("/response"), ::testing::_, ::testing::_))
        .WillOnce(Return(make_202()));

    Runtime rt{"127.0.0.1:9001", std::unique_ptr<HttpClient>(mock)};

    bool handler_called = false;
    rt.run([&](const Invocation &inv) {
        handler_called = true;
        EXPECT_EQ(inv.request_id(), "req-1");
        EXPECT_EQ(inv.payload(), R"({"hello":"world"})");
        return InvocationResponse::success(R"({"ok":true})");
    });

    EXPECT_TRUE(handler_called);
}

// When the handler throws, the runtime posts an error (not a success response).
TEST(Runtime, HandlerExceptionBecomesInvocationError) {
    auto *mock = new MockHttpClient;

    EXPECT_CALL(*mock, get(::testing::HasSubstr("/invocation/next")))
        .WillOnce(Return(make_next_response("req-2", "{}")))
        .WillOnce(Return(Outcome<HttpResponse>::failure("NetworkError", "gone")));

    // /error must be called, not /response.
    EXPECT_CALL(*mock, post(::testing::HasSubstr("/error"), ::testing::_, ::testing::_))
        .WillOnce(Return(make_202()));

    Runtime rt{"127.0.0.1:9001", std::unique_ptr<HttpClient>(mock)};

    rt.run([](const Invocation &) -> InvocationResponse {
        throw std::runtime_error("boom");
    });
}

// When the handler returns an error response (is_success == false), the runtime
// posts to /error rather than /response.
TEST(Runtime, HandlerErrorResponseGoesToErrorEndpoint) {
    auto *mock = new MockHttpClient;

    EXPECT_CALL(*mock, get(::testing::HasSubstr("/invocation/next")))
        .WillOnce(Return(make_next_response("req-3", "{}")))
        .WillOnce(Return(Outcome<HttpResponse>::failure("NetworkError", "gone")));

    EXPECT_CALL(*mock, post(::testing::HasSubstr("/error"), ::testing::_, ::testing::_))
        .WillOnce(Return(make_202()));

    Runtime rt{"127.0.0.1:9001", std::unique_ptr<HttpClient>(mock)};

    rt.run([](const Invocation &) {
        return InvocationResponse::error("ValidationError", "bad input");
    });
}

// The runtime exits immediately when the very first GET /next fails.
TEST(Runtime, ExitsOnNetworkFailureAtPoll) {
    auto *mock = new MockHttpClient;

    EXPECT_CALL(*mock, get(::testing::HasSubstr("/invocation/next")))
        .WillOnce(Return(Outcome<HttpResponse>::failure("CurlError", "connection refused")));

    // No POST should ever be made.
    EXPECT_CALL(*mock, post(::testing::_, ::testing::_, ::testing::_)).Times(0);

    Runtime rt{"127.0.0.1:9001", std::unique_ptr<HttpClient>(mock)};
    rt.run([](const Invocation &) { return InvocationResponse::success("{}"); });
}

// GET /next returns 200 but is missing the required request-id header.
TEST(Runtime, MissingRequestIdHeaderExitsRuntime) {
    auto *mock = new MockHttpClient;

    HttpResponse bad_response;
    bad_response.status_code = 200;
    bad_response.body        = "{}";
    // No lambda-runtime-aws-request-id header.

    EXPECT_CALL(*mock, get(::testing::HasSubstr("/invocation/next")))
        .WillOnce(Return(Outcome<HttpResponse>::success(std::move(bad_response))));

    EXPECT_CALL(*mock, post(::testing::_, ::testing::_, ::testing::_)).Times(0);

    Runtime rt{"127.0.0.1:9001", std::unique_ptr<HttpClient>(mock)};
    rt.run([](const Invocation &) { return InvocationResponse::success("{}"); });
}
