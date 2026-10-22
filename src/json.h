#pragma once

#include <string>
#include <utility>
#include <vector>

namespace jsonx {

class Json {
   public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;

    bool isNull() const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }

    const Json *find(const std::string &key) const;
    const Json *at(size_t index) const;

    std::string getString(const std::string &key, const std::string &fallback = "") const;
    double getNumber(const std::string &key, double fallback = 0) const;
    int getInt(const std::string &key, int fallback = 0) const;

    // Returns false and fills `error` on malformed input.
    static bool parse(const std::string &text, Json &out, std::string &error);
};

} // namespace jsonx
