#include "lambda_runtime/invocation.h"
#include <chrono>
#include <gtest/gtest.h>

using namespace lambda_runtime;

// ── Invocation ────────────────────────────────────────────────────────────────

TEST(Invocation, StoresRequiredFields) {
    Invocation inv{"req-123", R"({"key":"val"})", 9999999999ULL,
                   std::nullopt, std::nullopt, std::nullopt};

    EXPECT_EQ(inv.request_id(), "req-123");
    EXPECT_EQ(inv.payload(), R"({"key":"val"})");
    EXPECT_EQ(inv.deadline_epoch_ms(), 9999999999ULL);
}

TEST(Invocation, OptionalFieldsAbsent) {
    Invocation inv{"id", "{}", 0ULL, std::nullopt, std::nullopt, std::nullopt};

    EXPECT_FALSE(inv.invoked_function_arn().has_value());
    EXPECT_FALSE(inv.client_context().has_value());
    EXPECT_FALSE(inv.cognito_identity().has_value());
}

TEST(Invocation, OptionalFieldsPresent) {
    Invocation inv{"id", "{}", 0ULL,
                   "arn:aws:lambda:us-east-1:123:function:foo",
                   "base64ctx",
                   "cognitoId"};

    ASSERT_TRUE(inv.invoked_function_arn().has_value());
    EXPECT_EQ(*inv.invoked_function_arn(), "arn:aws:lambda:us-east-1:123:function:foo");
    ASSERT_TRUE(inv.client_context().has_value());
    EXPECT_EQ(*inv.client_context(), "base64ctx");
    ASSERT_TRUE(inv.cognito_identity().has_value());
    EXPECT_EQ(*inv.cognito_identity(), "cognitoId");
}

TEST(Invocation, RemainingTimeDeadlineInFuture) {
    using namespace std::chrono;
    // deadline = now + 30 seconds
    auto now_ms = static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    std::uint64_t deadline = now_ms + 30'000ULL;

    Invocation inv{"id", "{}", deadline, std::nullopt, std::nullopt, std::nullopt};

    auto remaining = inv.remaining_time();
    // Should be in (0, 30s] — allow a second of slack for test execution time.
    EXPECT_GT(remaining.count(), 0);
    EXPECT_LE(remaining.count(), 30'000);
}

TEST(Invocation, RemainingTimeDeadlineInPastReturnsZero) {
    // deadline = epoch 1 ms — far in the past
    Invocation inv{"id", "{}", 1ULL, std::nullopt, std::nullopt, std::nullopt};
    EXPECT_EQ(inv.remaining_time().count(), 0);
}

// ── InvocationResponse ────────────────────────────────────────────────────────

TEST(InvocationResponse, SuccessIsMarkedSuccess) {
    auto r = InvocationResponse::success(R"({"result":42})");
    EXPECT_TRUE(r.is_success);
    EXPECT_EQ(r.payload, R"({"result":42})");
}

TEST(InvocationResponse, ErrorIsMarkedFailure) {
    auto r = InvocationResponse::error("RuntimeError", "something went wrong");
    EXPECT_FALSE(r.is_success);
    // Payload must contain both fields as JSON
    EXPECT_NE(r.payload.find("RuntimeError"), std::string::npos);
    EXPECT_NE(r.payload.find("something went wrong"), std::string::npos);
}
