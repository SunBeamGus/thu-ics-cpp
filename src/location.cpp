#include "location.h"

#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace location {
namespace {

// 全角 ASCII（U+FF01..U+FF5E）与表意空格（U+3000）转半角，只处理 UTF-8 输入。
std::string toHalfWidth(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        // U+3000 全角空格：E3 80 80
        if (c == 0xE3 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
            static_cast<unsigned char>(text[i + 2]) == 0x80) {
            out.push_back(' ');
            i += 3;
            continue;
        }
        // EF BC 81..BF -> U+FF01..U+FF3F -> ASCII 0x21..0x5F
        if (c == 0xEF && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xBC) {
            unsigned char third = static_cast<unsigned char>(text[i + 2]);
            if (third >= 0x81 && third <= 0xBF) {
                out.push_back(static_cast<char>(0x21 + (third - 0x81)));
                i += 3;
                continue;
            }
        }
        // EF BD 80..9E -> U+FF40..U+FF5E -> ASCII 0x60..0x7E
        if (c == 0xEF && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xBD) {
            unsigned char third = static_cast<unsigned char>(text[i + 2]);
            if (third >= 0x80 && third <= 0x9E) {
                out.push_back(static_cast<char>(0x60 + (third - 0x80)));
                i += 3;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

std::string withoutSpaces(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == ' ' || c == '\t') continue;
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> splitCsv(const std::string &line) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == ',') {
            fields.push_back(util::trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(util::trim(current));
    return fields;
}

} // namespace

std::string Resolver::normalizeBuilding(const std::string &raw) {
    std::string s = withoutSpaces(toHalfWidth(raw));
    if (s.empty()) return s;

    // "第X教学楼" / "第X教" -> "X教"
    if (s.size() >= 3 && s.compare(0, 3, "第") == 0) s = s.substr(3);

    // 去掉 "学楼" 后缀："三教学楼" -> "三教"
    size_t pos = s.find("教学楼");
    if (pos != std::string::npos) s = s.substr(0, pos) + "教";

    // "3教" -> "三教"
    static const char *kCn[] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
    if (s.size() >= 2 && s[0] >= '1' && s[0] <= '9' && s.compare(1, 3, "教") == 0) {
        s = std::string(kCn[s[0] - '0']) + s.substr(1);
    }
    return s;
}

void Resolver::split(const std::string &raw, std::string &building, std::string &room) {
    building.clear();
    room.clear();
    std::string cleaned = withoutSpaces(toHalfWidth(raw));
    if (cleaned.empty()) return;

    // 第一个 ASCII 数字起算作教室号；之前的部分当楼名。
    size_t digit = cleaned.size();
    for (size_t i = 0; i < cleaned.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(cleaned[i]);
        if (c < 0x80 && isAsciiDigit(static_cast<char>(c))) {
            digit = i;
            break;
        }
    }
    building = normalizeBuilding(cleaned.substr(0, digit));
    if (digit < cleaned.size()) room = cleaned.substr(digit);
}

size_t Resolver::loadFromText(const std::string &text, std::vector<std::string> *problems) {
    size_t loaded = 0;
    size_t lineNumber = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? text.size() + 1 : eol + 1;
        ++lineNumber;

        std::string trimmed = util::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        std::vector<std::string> fields = splitCsv(trimmed);
        if (fields.size() < 2) {
            if (problems) problems->push_back("第 " + std::to_string(lineNumber) + " 行：至少要有 alias,name 两列");
            continue;
        }
        // 表头（alias,name,lat,lon）不算一条记录，无论它在第几行。
        if (util::toLower(fields[0]) == "alias") continue;
        std::string key = normalizeBuilding(fields[0]);
        if (key.empty() || fields[1].empty()) {
            if (problems) problems->push_back("第 " + std::to_string(lineNumber) + " 行：alias 或 name 为空");
            continue;
        }
        Entry entry;
        entry.name = fields[1];
        if (fields.size() >= 4) {
            entry.lat = fields[2];
            entry.lon = fields[3];
        }
        entries_[key] = entry;
        ++loaded;
    }
    return loaded;
}

bool Resolver::load(const std::string &path, std::vector<std::string> *problems) {
    std::string text;
    if (!util::readFile(path, text)) return false;
    loadFromText(text, problems);
    return true;
}

Place Resolver::resolve(const std::string &raw) {
    Place place;
    place.label = util::trim(raw);
    if (place.label.empty()) return place;

    std::string building;
    std::string room;
    split(place.label, building, room);

    auto it = entries_.find(building);
    if (it == entries_.end()) {
        ++unmatched_[place.label];
        return place;
    }

    place.matched = true;
    place.label = it->second.name;
    if (!room.empty()) place.label += " " + room;
    place.lat = it->second.lat;
    place.lon = it->second.lon;
    return place;
}

} // namespace location
