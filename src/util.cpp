#include "util.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace util {

std::string stripBom(const std::string &s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xef && static_cast<unsigned char>(s[1]) == 0xbb &&
        static_cast<unsigned char>(s[2]) == 0xbf) {
        return s.substr(3);
    }
    return s;
}

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string replaceAll(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string urlEncode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0f]);
        }
    }
    return out;
}

std::string hexEncode(const uint8_t *data, size_t len) {
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0f]);
    }
    return out;
}

std::string hexEncode(const std::vector<uint8_t> &data) {
    return hexEncode(data.data(), data.size());
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<uint8_t> hexDecode(const std::string &hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    int hi = -1;
    for (char c : hex) {
        int v = hexVal(c);
        if (v < 0) continue;
        if (hi < 0) {
            hi = v;
        } else {
            out.push_back(static_cast<uint8_t>((hi << 4) | v));
            hi = -1;
        }
    }
    return out;
}

std::string base64Encode(const std::vector<uint8_t> &data) {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(t[(v >> 18) & 63]);
        out.push_back(t[(v >> 12) & 63]);
        out.push_back(t[(v >> 6) & 63]);
        out.push_back(t[v & 63]);
        i += 3;
    }
    if (i + 1 == data.size()) {
        uint32_t v = data[i] << 16;
        out.push_back(t[(v >> 18) & 63]);
        out.push_back(t[(v >> 12) & 63]);
        out += "==";
    } else if (i + 2 == data.size()) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(t[(v >> 18) & 63]);
        out.push_back(t[(v >> 12) & 63]);
        out.push_back(t[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

static void appendUtf8(std::string &out, uint32_t cp) {
    if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0x10ffff) {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::string htmlDecode(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back(s[i]);
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        std::string decoded;
        if (ent == "amp") decoded = "&";
        else if (ent == "lt") decoded = "<";
        else if (ent == "gt") decoded = ">";
        else if (ent == "quot") decoded = "\"";
        else if (ent == "apos" || ent == "#39") decoded = "'";
        else if (ent == "nbsp") decoded = " ";
        else if (!ent.empty() && ent[0] == '#') {
            uint32_t cp = 0;
            bool hex = ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X');
            size_t start = hex ? 2 : 1;
            bool ok = start < ent.size();
            for (size_t k = start; ok && k < ent.size(); ++k) {
                int v = hexVal(ent[k]);
                if (v < 0 || (hex && v > 15)) {
                    ok = false;
                    break;
                }
                cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(v);
                if (cp > 0x10ffff) ok = false;
            }
            if (ok) appendUtf8(decoded, cp);
        }
        if (decoded.empty()) {
            out.push_back(s[i]);
        } else {
            out += decoded;
            i = semi;
        }
    }
    return out;
}

std::string stripTags(const std::string &html) {
    std::string out;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out.push_back(c);
    }
    return htmlDecode(out);
}

std::string gbkToUtf8(const std::string &s) {
    if (s.empty()) return s;
    int wlen = MultiByteToWideChar(936, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (wlen <= 0) return s;
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(936, 0, s.data(), static_cast<int>(s.size()), &w[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return s;
    std::string out(static_cast<size_t>(ulen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, &out[0], ulen, nullptr, nullptr);
    return out;
}

int daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

void civilFromDays(int z, int &y, int &m, int &d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
    y += (m <= 2);
}

std::string formatDate(int days) {
    int y, m, d;
    civilFromDays(days, y, m, d);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return buf;
}

std::string formatCompact(int days) {
    int y, m, d;
    civilFromDays(days, y, m, d);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d", y, m, d);
    return buf;
}

bool parseDateDays(const std::string &s, int &days) {
    size_t i = 0;
    while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i + 10 > s.size()) return false;
    auto digits = [&](size_t pos, size_t n, int &v) {
        v = 0;
        for (size_t k = 0; k < n; ++k) {
            char c = s[pos + k];
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            v = v * 10 + (c - '0');
        }
        return true;
    };
    int y, mo, d;
    if (!digits(i, 4, y)) return false;
    if (s[i + 4] != '-' && s[i + 4] != '/') return false;
    if (!digits(i + 5, 2, mo)) return false;
    if (s[i + 7] != '-' && s[i + 7] != '/') return false;
    if (!digits(i + 8, 2, d)) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    days = daysFromCivil(y, mo, d);
    return true;
}

bool parseHhmm(const std::string &s, int &minutes) {
    int h = -1, m = -1;
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) continue;
        size_t j = i;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        if (j >= s.size() || (s[j] != ':' && s[j] != '\xef')) continue;
        h = std::stoi(s.substr(i, j - i));
        size_t k = s[j] == '\xef' ? j + 3 : j + 1;
        if (k >= s.size() || !std::isdigit(static_cast<unsigned char>(s[k]))) return false;
        size_t l = k;
        while (l < s.size() && std::isdigit(static_cast<unsigned char>(s[l]))) ++l;
        m = std::stoi(s.substr(k, l - k));
        break;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    minutes = h * 60 + m;
    return true;
}

std::string nowUtcIcs() {
    SYSTEMTIME st;
    GetSystemTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond);
    return buf;
}

std::string foldIcsLine(const std::string &line) {
    if (line.size() <= 75) return line;
    std::string out;
    size_t offset = 0;
    size_t limit = 75;
    while (offset < line.size()) {
        size_t end = std::min(offset + limit, line.size());
        // Do not split a UTF-8 multi-byte sequence.
        while (end > offset && end < line.size() && (static_cast<unsigned char>(line[end]) & 0xc0) == 0x80) --end;
        if (!out.empty()) out += "\r\n ";
        out += line.substr(offset, end - offset);
        offset = end;
        limit = 74;
    }
    return out;
}

bool readFile(const std::string &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const std::string &path, const std::string &content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(f);
}

bool ensureParentDir(const std::string &path) {
    std::error_code ec;
    auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return true;
    std::filesystem::create_directories(parent, ec);
    return !ec;
}

} // namespace util
