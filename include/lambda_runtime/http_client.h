#pragma once

#include "outcome.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace lambda_runtime {

// Plain data returned by every HTTP call.
struct HttpResponse {
  long status_code{0};
  std::string body;
  std::unordered_map<std::string, std::string> headers;
};

// Abstract HTTP interface. Swap in a mock for unit tests.
class HttpClient {
public:
  virtual ~HttpClient() = default;

  [[nodiscard]] virtual Outcome<HttpResponse> get(const std::string &url) = 0;

  [[nodiscard]] virtual Outcome<HttpResponse>
  post(const std::string &url, const std::string &body,
       const std::string &content_type) = 0;
};

// Concrete libcurl implementation. Marked final — not meant to be subclassed.
class CurlHttpClient final : public HttpClient {
public:
  CurlHttpClient();
  ~CurlHttpClient() override = default;

  // Curl handles are not copyable or movable.
  CurlHttpClient(const CurlHttpClient &)            = delete;
  CurlHttpClient &operator=(const CurlHttpClient &) = delete;
  CurlHttpClient(CurlHttpClient &&)                 = delete;
  CurlHttpClient &operator=(CurlHttpClient &&)      = delete;

  [[nodiscard]] Outcome<HttpResponse> get(const std::string &url) override;

  [[nodiscard]] Outcome<HttpResponse>
  post(const std::string &url, const std::string &body,
       const std::string &content_type) override;

private:
  // Calls curl_easy_cleanup when handle_ is destroyed.
  struct CurlDeleter {
    void operator()(void *curl) const noexcept;
  };

  std::unique_ptr<void, CurlDeleter> handle_;

  static std::size_t write_callback(char *ptr, std::size_t size,
                                    std::size_t nmemb, void *userdata) noexcept;

  static std::size_t header_callback(char *buffer, std::size_t size,
                                     std::size_t nitems,
                                     void *userdata) noexcept;
};

} // namespace lambda_runtime
