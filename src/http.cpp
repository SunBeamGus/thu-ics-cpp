#include "http.h"

#include "util.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace http {
namespace {

const wchar_t *kUserAgent =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36";

std::wstring toWide(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string toUtf8(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::string winErrorText(DWORD code) {
    wchar_t *buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string text;
    if (n && buf) {
        std::wstring w(buf, n);
        while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ')) w.pop_back();
        text = toUtf8(w);
    }
    if (buf) LocalFree(buf);
    if (text.empty()) text = "WinHTTP error";
    char num[32];
    std::snprintf(num, sizeof(num), " (code %lu)", static_cast<unsigned long>(code));
    return text + num;
}

[[noreturn]] void fail(const std::string &what, DWORD code = 0) {
    if (code == 12185) {
        throw std::runtime_error(what + ": " + winErrorText(code) +
                                 "（当前进程无法访问 Windows SChannel 凭据，常见于沙箱/受限令牌；请在普通终端中运行）");
    }
    if (code) throw std::runtime_error(what + ": " + winErrorText(code));
    throw std::runtime_error(what);
}

struct UrlParts {
    std::wstring scheme;
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 443;
    bool secure = true;
};

bool crackUrl(const std::string &url, UrlParts &out, std::string &error) {
    std::wstring wurl = toWide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
        error = "invalid URL: " + url;
        return false;
    }
    out.scheme.assign(uc.lpszScheme, uc.dwSchemeLength);
    out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0) out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (out.path.empty()) out.path = L"/";
    out.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    out.port = uc.nPort;
    return true;
}

std::string resolveUrl(const std::string &base, const std::string &location) {
    if (util::startsWith(location, "http://") || util::startsWith(location, "https://")) return location;

    UrlParts parts;
    std::string error;
    if (!crackUrl(base, parts, error)) return location;

    std::string scheme = toUtf8(parts.scheme);
    std::string host = toUtf8(parts.host);
    std::string origin = scheme + "://" + host;
    if ((parts.secure && parts.port != 443) || (!parts.secure && parts.port != 80)) {
        origin += ":" + std::to_string(parts.port);
    }
    if (util::startsWith(location, "//")) return scheme + ":" + location;
    if (util::startsWith(location, "/")) return origin + location;

    std::string path = toUtf8(parts.path);
    size_t q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return origin + "/" + location;
    return origin + path.substr(0, slash + 1) + location;
}

bool isRedirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

std::string headerValue(const std::string &raw, const std::string &name, std::vector<std::string> *all = nullptr) {
    std::string lowerName = util::toLower(name);
    std::string result;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find("\r\n", pos);
        if (eol == std::string::npos) eol = raw.size();
        std::string line = raw.substr(pos, eol - pos);
        pos = eol + 2;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (util::toLower(util::trim(line.substr(0, colon))) != lowerName) continue;
        std::string value = util::trim(line.substr(colon + 1));
        if (all) all->push_back(value);
        if (result.empty()) result = value;
    }
    return result;
}

} // namespace

std::string urlEncodePairs(const std::vector<std::pair<std::string, std::string>> &fields) {
    std::string out;
    for (const auto &kv : fields) {
        if (!out.empty()) out.push_back('&');
        out += util::urlEncode(kv.first);
        out.push_back('=');
        out += util::urlEncode(kv.second);
    }
    return out;
}

HttpClient::HttpClient(std::string proxy) {
    DWORD access = WINHTTP_ACCESS_TYPE_NO_PROXY;
    const wchar_t *proxyName = WINHTTP_NO_PROXY_NAME;
    std::wstring proxyWide;
    if (!proxy.empty() && proxy != "system") {
        proxyWide = toWide(proxy);
        access = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        proxyName = proxyWide.c_str();
    } else if (proxy == "system") {
        access = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
    }
    session_ = WinHttpOpen(kUserAgent, access, proxyName, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ == nullptr) fail("WinHttpOpen failed", GetLastError());
    WinHttpSetTimeouts(static_cast<HINTERNET>(session_), 10000, 15000, 30000, 60000);
}

HttpClient::~HttpClient() {
    if (session_) WinHttpCloseHandle(static_cast<HINTERNET>(session_));
}

std::string HttpClient::cookieHeader() const {
    std::string out;
    for (const auto &kv : cookies_) {
        if (!out.empty()) out += "; ";
        out += kv.first + "=" + kv.second;
    }
    return out;
}

Response HttpClient::get(const std::string &url, int maxRedirects) {
    return request("GET", url, "", maxRedirects);
}

Response HttpClient::postForm(const std::string &url,
                              const std::vector<std::pair<std::string, std::string>> &fields, int maxRedirects) {
    return request("POST", url, urlEncodePairs(fields), maxRedirects);
}

Response HttpClient::request(const std::string &method, const std::string &url, const std::string &body,
                             int maxRedirects) {
    std::string current = url;
    std::string currentMethod = method;
    std::string currentBody = body;
    Response response;
    for (int hop = 0;; ++hop) {
        response = perform(currentMethod, current, currentBody);
        std::string location = response.finalUrl; // perform() stores the Location here temporarily
        if (isRedirect(response.status) && !location.empty() && hop < maxRedirects) {
            std::string next = resolveUrl(current, location);
            if (response.status == 303 || ((response.status == 301 || response.status == 302) && currentMethod != "GET")) {
                currentMethod = "GET";
                currentBody.clear();
            }
            current = next;
            continue;
        }
        break;
    }
    response.finalUrl = current;
    return response;
}

Response HttpClient::perform(const std::string &method, const std::string &url, const std::string &body) {
    UrlParts parts;
    std::string error;
    if (!crackUrl(url, parts, error)) fail(error);

    HINTERNET session = static_cast<HINTERNET>(session_);
    HINTERNET conn = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
    if (!conn) fail("WinHttpConnect failed", GetLastError());

    std::wstring methodW = toWide(method);
    HINTERNET req = WinHttpOpenRequest(conn, methodW.c_str(), parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       parts.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        DWORD code = GetLastError();
        WinHttpCloseHandle(conn);
        fail("WinHttpOpenRequest failed", code);
    }

    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));

    std::wstring headers;
    if (!body.empty()) headers += L"Content-Type: application/x-www-form-urlencoded\r\n";
    std::string cookie = cookieHeader();
    if (!cookie.empty()) headers += L"Cookie: " + toWide(cookie) + L"\r\n";

    BOOL sent = WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                   headers.empty() ? 0 : static_cast<DWORD>(-1),
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(body.data()),
                                   static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent) {
        DWORD code = GetLastError();
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        fail("WinHttpSendRequest failed for " + url, code);
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        DWORD code = GetLastError();
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        fail("WinHttpReceiveResponse failed for " + url, code);
    }

    Response response;
    DWORD status = 0;
    DWORD statusLen = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &statusLen, WINHTTP_NO_HEADER_INDEX);
    response.status = static_cast<int>(status);

    DWORD rawLen = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &rawLen,
                        WINHTTP_NO_HEADER_INDEX);
    std::string rawHeaders;
    if (rawLen > 0) {
        std::wstring raw(rawLen / sizeof(wchar_t) + 1, L'\0');
        DWORD len = static_cast<DWORD>(raw.size() * sizeof(wchar_t));
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, &raw[0], &len,
                                WINHTTP_NO_HEADER_INDEX)) {
            raw.resize(wcslen(raw.c_str()));
            rawHeaders = toUtf8(raw);
        }
    }

    std::vector<std::string> setCookies;
    headerValue(rawHeaders, "Set-Cookie", &setCookies);
    for (const std::string &sc : setCookies) {
        std::string first = sc.substr(0, sc.find(';'));
        size_t eq = first.find('=');
        if (eq == std::string::npos) continue;
        std::string name = util::trim(first.substr(0, eq));
        std::string value = util::trim(first.substr(eq + 1));
        std::string lower = util::toLower(sc);
        bool expired = lower.find("max-age=0") != std::string::npos;
        if (name.empty()) continue;
        if (value.empty() || expired) {
            cookies_.erase(name);
        } else {
            cookies_[name] = value;
        }
    }

    response.finalUrl = headerValue(rawHeaders, "Location");
    response.contentType = headerValue(rawHeaders, "Content-Type");

    DWORD available = 0;
    do {
        if (!WinHttpQueryDataAvailable(req, &available)) break;
        if (available == 0) break;
        std::vector<char> buf(available);
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), available, &read)) break;
        response.body.append(buf.data(), read);
    } while (available > 0);

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);

    std::string lowerType = util::toLower(response.contentType);
    bool gbk = lowerType.find("gb2312") != std::string::npos || lowerType.find("gbk") != std::string::npos ||
               lowerType.find("gb18030") != std::string::npos;
    if (!gbk) {
        // Some id.tsinghua.edu.cn error pages declare the charset only in <meta>.
        std::string head = util::toLower(response.body.substr(0, 2048));
        if (head.find("charset=gb2312") != std::string::npos || head.find("charset=gbk") != std::string::npos ||
            head.find("charset=gb18030") != std::string::npos) {
            gbk = true;
        }
    }
    if (gbk) response.body = util::gbkToUtf8(response.body);
    return response;
}

} // namespace http
