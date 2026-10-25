#pragma once

#include <map>
#include <string>
#include <vector>

namespace location {

struct Place {
    std::string label; // 日历里显示的写法（规范化楼名 + 教室号）
    std::string lat;   // 空表示没有坐标
    std::string lon;
    bool matched = false;
};

struct Entry {
    std::string name;
    std::string lat;
    std::string lon;
};

// 把课程表里的地点串（如 "三教1101"）解析成显示名 + 楼级坐标。
// 数据来自一个简单的 CSV：alias,name,lat,lon（# 开头与空行忽略）。
// 匹配不到时保留原始串，并计入 unmatched()，方便人工补齐映射表。
class Resolver {
   public:
    size_t loadFromText(const std::string &text, std::vector<std::string> *problems = nullptr);
    bool load(const std::string &path, std::vector<std::string> *problems = nullptr);

    Place resolve(const std::string &raw);

    const std::map<std::string, size_t> &unmatched() const { return unmatched_; }
    size_t size() const { return entries_.size(); }

    // 供自检使用的拆分/规范化规则：
    //   "六教6A216" -> building "六教", room "6A216"
    //   "第三教学楼1101" -> building "三教", room "1101"（规范化后）
    static void split(const std::string &raw, std::string &building, std::string &room);
    static std::string normalizeBuilding(const std::string &raw);

   private:
    std::map<std::string, Entry> entries_;    // 规范化后的 alias -> Entry
    std::map<std::string, size_t> unmatched_; // 原始串 -> 出现次数
};

} // namespace location
