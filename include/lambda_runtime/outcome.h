#pragma once

#include <stdexcept>
#include <string>
#include <variant>

namespace lambda_runtime {

// Structured error carried by all failed Outcomes.
struct ErrorInfo {
  std::string error_type;
  std::string error_message;
};

// Either a success value T or an ErrorInfo. Check is_success() before access.
template <typename T> class [[nodiscard]] Outcome {
public:
  [[nodiscard]] static Outcome success(T value) {
    return Outcome{std::in_place_index<0>, std::move(value)};
  }

  [[nodiscard]] static Outcome failure(std::string error_type,
                                       std::string error_message) {
    return Outcome{std::in_place_index<1>,
                   ErrorInfo{std::move(error_type), std::move(error_message)}};
  }

  [[nodiscard]] bool is_success() const noexcept {
    return std::holds_alternative<T>(data_);
  }
  [[nodiscard]] bool is_failure() const noexcept { return !is_success(); }

  [[nodiscard]] const T &value() const & {
    if (is_failure())
      throw std::logic_error("Accessing value of a failed Outcome");
    return std::get<T>(data_);
  }
  [[nodiscard]] T &value() & {
    if (is_failure())
      throw std::logic_error("Accessing value of a failed Outcome");
    return std::get<T>(data_);
  }
  [[nodiscard]] T take_value() && {
    if (is_failure())
      throw std::logic_error("Taking value of a failed Outcome");
    return std::get<T>(std::move(data_));
  }

  [[nodiscard]] const ErrorInfo &error() const {
    if (is_success())
      throw std::logic_error("Accessing error of a successful Outcome");
    return std::get<ErrorInfo>(data_);
  }

private:
  template <std::size_t I, typename... Args>
  explicit Outcome(std::in_place_index_t<I> idx, Args &&...args)
      : data_{idx, std::forward<Args>(args)...} {}

  std::variant<T, ErrorInfo> data_;
};

// Outcome<void> — for operations that succeed with no value or fail with an error.
template <> class [[nodiscard]] Outcome<void> {
public:
  [[nodiscard]] static Outcome success() noexcept {
    return Outcome{std::monostate{}};
  }
  [[nodiscard]] static Outcome failure(std::string error_type,
                                       std::string error_message) {
    return Outcome{ErrorInfo{std::move(error_type), std::move(error_message)}};
  }

  [[nodiscard]] bool is_success() const noexcept {
    return std::holds_alternative<std::monostate>(data_);
  }
  [[nodiscard]] bool is_failure() const noexcept { return !is_success(); }

  [[nodiscard]] const ErrorInfo &error() const {
    if (is_success())
      throw std::logic_error("Accessing error of a successful Outcome<void>");
    return std::get<ErrorInfo>(data_);
  }

private:
  explicit Outcome(std::monostate m) noexcept : data_{m} {}
  explicit Outcome(ErrorInfo e) : data_{std::move(e)} {}

  std::variant<std::monostate, ErrorInfo> data_;
};

} // namespace lambda_runtime
