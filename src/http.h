#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace http {

struct Response {
    int status = 0;
    std::string body;
    std::string finalUrl;
    std::string contentType;
};

class HttpClient {
   public:
    // `proxy`: empty for direct connection, "system" for the WinHTTP machine
    // proxy, otherwise "host:port" / "http://host:port".
    explicit HttpClient(std::string proxy = "");
    ~HttpClient();

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    Response get(const std::string &url, int maxRedirects = 10);
    Response postForm(const std::string &url, const std::vector<std::pair<std::string, std::string>> &fields,
                      int maxRedirects = 10);

    const std::map<std::string, std::string> &cookies() const { return cookies_; }
    void clearCookies() { cookies_.clear(); }
    std::string cookieHeader() const;

   private:
    Response request(const std::string &method, const std::string &url, const std::string &body, int maxRedirects);
    Response perform(const std::string &method, const std::string &url, const std::string &body);

    void *session_ = nullptr;
    std::map<std::string, std::string> cookies_;
};

std::string urlEncodePairs(const std::vector<std::pair<std::string, std::string>> &fields);

} // namespace http
