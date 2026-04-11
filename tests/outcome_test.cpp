#include "lambda_runtime/outcome.h"
#include <gtest/gtest.h>
#include <string>

using namespace lambda_runtime;

// ── Outcome<int> ──────────────────────────────────────────────────────────────

TEST(OutcomeInt, SuccessHoldsValue) {
    auto o = Outcome<int>::success(42);
    EXPECT_TRUE(o.is_success());
    EXPECT_FALSE(o.is_failure());
    EXPECT_EQ(o.value(), 42);
}

TEST(OutcomeInt, FailureHoldsError) {
    auto o = Outcome<int>::failure("NetworkError", "connection refused");
    EXPECT_FALSE(o.is_success());
    EXPECT_TRUE(o.is_failure());
    EXPECT_EQ(o.error().error_type, "NetworkError");
    EXPECT_EQ(o.error().error_message, "connection refused");
}

TEST(OutcomeInt, AccessingValueOfFailureThrows) {
    auto o = Outcome<int>::failure("E", "msg");
    EXPECT_THROW((void)o.value(), std::logic_error);
}

TEST(OutcomeInt, AccessingErrorOfSuccessThrows) {
    auto o = Outcome<int>::success(1);
    EXPECT_THROW((void)o.error(), std::logic_error);
}

TEST(OutcomeInt, TakeValueMovesOut) {
    auto o = Outcome<std::string>::success("hello");
    std::string moved = std::move(o).take_value();
    EXPECT_EQ(moved, "hello");
}

// ── Outcome<void> ─────────────────────────────────────────────────────────────

TEST(OutcomeVoid, SuccessIsSuccess) {
    auto o = Outcome<void>::success();
    EXPECT_TRUE(o.is_success());
    EXPECT_FALSE(o.is_failure());
}

TEST(OutcomeVoid, FailureHoldsError) {
    auto o = Outcome<void>::failure("Timeout", "deadline exceeded");
    EXPECT_TRUE(o.is_failure());
    EXPECT_EQ(o.error().error_type, "Timeout");
    EXPECT_EQ(o.error().error_message, "deadline exceeded");
}

TEST(OutcomeVoid, AccessingErrorOfSuccessThrows) {
    auto o = Outcome<void>::success();
    EXPECT_THROW((void)o.error(), std::logic_error);
}
