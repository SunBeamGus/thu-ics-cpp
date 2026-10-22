#include "json.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jsonx {
namespace {

void appendUtf8(std::string &out, uint32_t cp) {
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

class Parser {
   public:
    Parser(const std::string &text, std::string &error) : s_(text), err_(error) {}

    bool parse(Json &out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        if (pos_ != s_.size()) {
            fail("trailing characters");
            return false;
        }
        return true;
    }

   private:
    const std::string &s_;
    std::string &err_;
    size_t pos_ = 0;

    void fail(const std::string &msg) {
        if (err_.empty()) err_ = msg + " at offset " + std::to_string(pos_);
    }

    void skipWs() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool parseValue(Json &out) {
        skipWs();
        if (pos_ >= s_.size()) {
            fail("unexpected end of input");
            return false;
        }
        char c = s_[pos_];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            out.type = Json::Type::String;
            return parseString(out.string);
        }
        if (c == 't' || c == 'f') {
            if (s_.compare(pos_, 4, "true") == 0) {
                pos_ += 4;
                out.type = Json::Type::Bool;
                out.boolean = true;
                return true;
            }
            if (s_.compare(pos_, 5, "false") == 0) {
                pos_ += 5;
                out.type = Json::Type::Bool;
                out.boolean = false;
                return true;
            }
            fail("invalid literal");
            return false;
        }
        if (c == 'n') {
            if (s_.compare(pos_, 4, "null") == 0) {
                pos_ += 4;
                out.type = Json::Type::Null;
                return true;
            }
            fail("invalid literal");
            return false;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(out);
        fail("unexpected character");
        return false;
    }

    bool parseNumber(Json &out) {
        const char *start = s_.c_str() + pos_;
        char *end = nullptr;
        double v = std::strtod(start, &end);
        if (end == start) {
            fail("invalid number");
            return false;
        }
        pos_ += static_cast<size_t>(end - start);
        out.type = Json::Type::Number;
        out.number = v;
        return true;
    }

    bool parseHex4(uint32_t &v) {
        if (pos_ + 4 > s_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            v = (v << 4) | static_cast<uint32_t>(d);
        }
        return true;
    }

    bool parseString(std::string &out) {
        if (!consume('"')) return false;
        out.clear();
        while (pos_ < s_.size()) {
            unsigned char c = static_cast<unsigned char>(s_[pos_++]);
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) {
                    fail("unterminated escape");
                    return false;
                }
                char e = s_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!parseHex4(cp)) {
                            fail("invalid \\u escape");
                            return false;
                        }
                        if (cp >= 0xd800 && cp <= 0xdbff && pos_ + 6 <= s_.size() && s_[pos_] == '\\' &&
                            s_[pos_ + 1] == 'u') {
                            size_t save = pos_;
                            pos_ += 2;
                            uint32_t low = 0;
                            if (parseHex4(low) && low >= 0xdc00 && low <= 0xdfff) {
                                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                            } else {
                                pos_ = save;
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default:
                        fail("invalid escape");
                        return false;
                }
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        fail("unterminated string");
        return false;
    }

    bool parseArray(Json &out) {
        if (!consume('[')) return false;
        out.type = Json::Type::Array;
        skipWs();
        if (consume(']')) return true;
        for (;;) {
            Json item;
            if (!parseValue(item)) return false;
            out.array.push_back(std::move(item));
            skipWs();
            if (consume(',')) continue;
            if (consume(']')) return true;
            fail("expected ',' or ']'");
            return false;
        }
    }

    bool parseObject(Json &out) {
        if (!consume('{')) return false;
        out.type = Json::Type::Object;
        skipWs();
        if (consume('}')) return true;
        for (;;) {
            skipWs();
            std::string key;
            if (!parseString(key)) {
                fail("expected object key");
                return false;
            }
            skipWs();
            if (!consume(':')) {
                fail("expected ':'");
                return false;
            }
            Json value;
            if (!parseValue(value)) return false;
            out.object.emplace_back(std::move(key), std::move(value));
            skipWs();
            if (consume(',')) continue;
            if (consume('}')) return true;
            fail("expected ',' or '}'");
            return false;
        }
    }
};

} // namespace

const Json *Json::find(const std::string &key) const {
    if (type != Type::Object) return nullptr;
    for (const auto &kv : object) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

const Json *Json::at(size_t index) const {
    if (type != Type::Array || index >= array.size()) return nullptr;
    return &array[index];
}

std::string Json::getString(const std::string &key, const std::string &fallback) const {
    const Json *v = find(key);
    if (v == nullptr) return fallback;
    if (v->type == Type::String) return v->string;
    if (v->type == Type::Number) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f", v->number);
        return buf;
    }
    return fallback;
}

double Json::getNumber(const std::string &key, double fallback) const {
    const Json *v = find(key);
    if (v == nullptr || v->type != Type::Number) return fallback;
    return v->number;
}

int Json::getInt(const std::string &key, int fallback) const {
    const Json *v = find(key);
    if (v == nullptr || v->type != Type::Number) return fallback;
    return static_cast<int>(v->number);
}

bool Json::parse(const std::string &text, Json &out, std::string &error) {
    std::string cleaned = text;
    if (cleaned.size() >= 3 && static_cast<unsigned char>(cleaned[0]) == 0xef &&
        static_cast<unsigned char>(cleaned[1]) == 0xbb && static_cast<unsigned char>(cleaned[2]) == 0xbf) {
        cleaned = cleaned.substr(3);
    }
    Parser p(cleaned, error);
    out = Json{};
    return p.parse(out);
}

} // namespace jsonx
