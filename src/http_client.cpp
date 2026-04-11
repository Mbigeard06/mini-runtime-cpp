#include "lambda_runtime/http_client.h"
#include <curl/curl.h>
#include <stdexcept>
#include <string>

namespace lambda_runtime {

namespace {

// Ensures curl_global_init runs exactly once per process.
struct CurlGlobalGuard {
  CurlGlobalGuard() {
    // Initialize curl library
    if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
      throw std::runtime_error("Failed to initialize libcurl");
    }
  }
  ~CurlGlobalGuard() { curl_global_cleanup(); }
};

void ensure_curl_initialized() {
  static CurlGlobalGuard guard; // constructed once, destroyed at exit
  (void)guard;
}

} // namespace

void CurlHttpClient::CurlDeleter::operator()(void *curl) const noexcept {
  curl_easy_cleanup(static_cast<CURL *>(curl));
}

CurlHttpClient::CurlHttpClient() {
  ensure_curl_initialized();
  CURL *curlPtr = curl_easy_init();
  if (curlPtr == nullptr)
    throw std::runtime_error("curll null");
  handle_.reset(curlPtr);
}

// Called by curl to write response body chunks into our string.
std::size_t CurlHttpClient::write_callback(char *ptr, std::size_t size,
                                           std::size_t nmemb,
                                           void *userdata) noexcept {
  auto *body = static_cast<std::string *>(userdata);
  body->append(ptr, size * nmemb);
  return size * nmemb; // must return bytes consumed or curl aborts
}

// Called by curl for each response header line.
std::size_t CurlHttpClient::header_callback(char *buffer, std::size_t size,
                                            std::size_t nitems,
                                            void *userdata) noexcept {
  auto *headers =
      static_cast<std::unordered_map<std::string, std::string> *>(userdata);
  std::string line(buffer, size * nitems);
  auto colon = line.find(':');
  if (colon == std::string::npos)
    return size * nitems;

  std::string key = line.substr(0, colon);
  std::string value = line.substr(colon + 1);

  std::transform(key.begin(), key.end(), key.begin(), ::tolower);

  auto start = value.find_first_not_of(" \t\r\n");
  if (start != std::string::npos)
    value = value.substr(start);
  auto end = value.find_last_not_of(" \t\r\n");
  if (end != std::string::npos)
    value = value.substr(0, end + 1);

  (*headers)[key] = value;
  return size * nitems; // must return bytes consumed or curl aborts
}

Outcome<HttpResponse> CurlHttpClient::get(const std::string &url) {
  CURL *curl = static_cast<CURL *>(handle_.get());
  curl_easy_reset(curl);
  HttpResponse response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback);
  // Indicate where to write user data
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &header_callback);
  // Indicate where to write header data
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

  const CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    return Outcome<HttpResponse>::failure("CurlError", curl_easy_strerror(res));
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
  return Outcome<HttpResponse>::success(std::move(response));
}

Outcome<HttpResponse> CurlHttpClient::post(const std::string &url,
                                           const std::string &body,
                                           const std::string &content_type) {
  CURL *curl = static_cast<CURL *>(handle_.get());
  curl_easy_reset(curl);

  HttpResponse response;

  // Build "Content-Type: application/json" header list for this request.
  struct curl_slist *headers = nullptr;
  headers =
      curl_slist_append(headers, ("Content-Type: " + content_type).c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

  const CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers); // free header list regardless of outcome

  if (res != CURLE_OK) {
    return Outcome<HttpResponse>::failure("CurlError", curl_easy_strerror(res));
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
  return Outcome<HttpResponse>::success(std::move(response));
}

} // namespace lambda_runtime
