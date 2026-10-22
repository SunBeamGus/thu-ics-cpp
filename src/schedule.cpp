#include "schedule.h"

#include "json.h"
#include "util.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace schedule {
namespace {

struct Period {
    const char *begin;
    const char *end;
};

const Period kPeriods[14] = {
    {"08:00", "08:45"}, {"08:50", "09:35"}, {"09:50", "10:35"}, {"10:40", "11:25"}, {"11:30", "12:15"},
    {"13:30", "14:15"}, {"14:20", "15:05"}, {"15:20", "16:05"}, {"16:10", "16:55"}, {"17:05", "17:50"},
    {"17:55", "18:40"}, {"19:20", "20:05"}, {"20:10", "20:55"}, {"21:00", "21:45"},
};

int weekdayFromDays(int days) { return ((days % 7) + 7 + 3) % 7 + 1; }

std::string keyFor(const Course &course) { return course.name + "\x1f" + course.location; }

Course &findOrCreate(std::vector<Course> &courses, std::map<std::string, size_t> &index, const std::string &name,
                     const std::string &location, Type type, const std::string &hash) {
    std::string key = name + "\x1f" + location;
    auto it = index.find(key);
    if (it != index.end()) return courses[it->second];
    Course course;
    course.name = name;
    course.location = location;
    course.type = type;
    course.hash = hash;
    courses.push_back(course);
    index[key] = courses.size() - 1;
    return courses.back();
}

void addSlice(Course &course, const TimeSlice &slice) {
    for (TimeSlice &existing : course.slices) {
        if (existing.dayOfWeek == slice.dayOfWeek && existing.begin == slice.begin && existing.end == slice.end) {
            existing.weeks.insert(existing.weeks.end(), slice.weeks.begin(), slice.weeks.end());
            std::sort(existing.weeks.begin(), existing.weeks.end());
            existing.weeks.erase(std::unique(existing.weeks.begin(), existing.weeks.end()), existing.weeks.end());
            return;
        }
    }
    TimeSlice copy = slice;
    std::sort(copy.weeks.begin(), copy.weeks.end());
    copy.weeks.erase(std::unique(copy.weeks.begin(), copy.weeks.end()), copy.weeks.end());
    course.slices.push_back(copy);
}

std::string jsUnescape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        char n = s[++i];
        switch (n) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '"': out.push_back('"'); break;
            case '\'': out.push_back('\''); break;
            case '\\': out.push_back('\\'); break;
            default:
                out.push_back('\\');
                out.push_back(n);
                break;
        }
    }
    return out;
}

void parseWeeksSpec(const std::string &spec, std::vector<int> &weeks) {
    for (const std::string &raw : util::split(spec, ',')) {
        std::string segment = util::trim(raw);
        if (segment.empty()) continue;
        size_t dash = segment.find_first_of("-~");
        if (dash == std::string::npos) {
            int v = std::atoi(segment.c_str());
            if (v > 0) weeks.push_back(v);
        } else {
            int a = std::atoi(segment.substr(0, dash).c_str());
            int b = std::atoi(segment.substr(dash + 1).c_str());
            if (a > 0 && b >= a && b <= 60) {
                for (int w = a; w <= b; ++w) weeks.push_back(w);
            }
        }
    }
}

std::vector<int> secondaryWeeks(const std::string &detail) {
    std::vector<int> weeks;
    if (detail.find("单周") != std::string::npos) {
        for (int w = 1; w <= 15; w += 2) weeks.push_back(w);
    } else if (detail.find("双周") != std::string::npos) {
        for (int w = 2; w <= 16; w += 2) weeks.push_back(w);
    } else if (detail.find("全周") != std::string::npos) {
        for (int w = 1; w <= 16; ++w) weeks.push_back(w);
    } else if (detail.find("前八周") != std::string::npos || detail.find("前8周") != std::string::npos) {
        for (int w = 1; w <= 8; ++w) weeks.push_back(w);
    } else if (detail.find("后八周") != std::string::npos || detail.find("后8周") != std::string::npos) {
        for (int w = 9; w <= 16; ++w) weeks.push_back(w);
    } else {
        static const std::regex zhRe("第([\\d\\-~,]+)周");
        static const std::regex enRe("Week([\\d\\-~,]+)", std::regex::icase);
        std::smatch m;
        if (std::regex_search(detail, m, zhRe)) {
            parseWeeksSpec(m[1].str(), weeks);
        } else if (std::regex_search(detail, m, enRe)) {
            parseWeeksSpec(m[1].str(), weeks);
        }
    }
    std::sort(weeks.begin(), weeks.end());
    weeks.erase(std::unique(weeks.begin(), weeks.end()), weeks.end());
    return weeks;
}

} // namespace

int periodBeginMinutes(int period) {
    if (period < 1) period = 1;
    if (period > 14) period = 14;
    int minutes = 0;
    util::parseHhmm(kPeriods[period - 1].begin, minutes);
    return minutes;
}

int periodEndMinutes(int period) {
    if (period < 1) period = 1;
    if (period > 14) period = 14;
    int minutes = 0;
    util::parseHhmm(kPeriods[period - 1].end, minutes);
    return minutes;
}

bool periodFromBegin(const std::string &hhmm, int &period) {
    int minutes = 0;
    if (!util::parseHhmm(hhmm, minutes)) return false;
    for (int i = 0; i < 14; ++i) {
        if (periodBeginMinutes(i + 1) == minutes) {
            period = i + 1;
            return true;
        }
    }
    return false;
}

bool periodFromEnd(const std::string &hhmm, int &period) {
    int minutes = 0;
    if (!util::parseHhmm(hhmm, minutes)) return false;
    for (int i = 0; i < 14; ++i) {
        if (periodEndMinutes(i + 1) == minutes) {
            period = i + 1;
            return true;
        }
    }
    return false;
}

thu::CalendarData parseCalendarJson(const std::string &raw) {
    jsonx::Json json;
    std::string error;
    if (!jsonx::Json::parse(raw, json, error)) throw std::runtime_error("学期列表 JSON 解析失败：" + error);
    if (json.getString("message") != "success") throw std::runtime_error("学期列表接口返回失败。");

    auto parseSemester = [](const jsonx::Json &item) {
        thu::Semester semester;
        int start = 0, end = 0;
        if (!util::parseDateDays(item.getString("kssj"), start) ||
            !util::parseDateDays(item.getString("jssj"), end)) {
            throw std::runtime_error("学期日期格式异常。");
        }
        semester.firstDay = util::formatDate(start + 2);
        semester.semesterId = item.getString("id");
        semester.semesterName = item.getString("xnxqmc");
        semester.weekCount = (end - start) / 7 + 1;
        if (semester.weekCount < 1) semester.weekCount = 1;
        if (semester.weekCount > 30) semester.weekCount = 30;
        return semester;
    };

    const jsonx::Json *result = json.find("result");
    if (result == nullptr) throw std::runtime_error("学期列表缺少 result 字段。");
    thu::CalendarData data;
    data.current = parseSemester(*result);
    if (const jsonx::Json *list = json.find("resultList")) {
        if (list->type == jsonx::Json::Type::Array) {
            for (const jsonx::Json &item : list->array) {
                if (item.type == jsonx::Json::Type::Object) data.next.push_back(parseSemester(item));
            }
        }
    }
    return data;
}

ParseResult parsePrimaryJson(const std::string &raw, const thu::Semester &semester) {
    ParseResult result;
    jsonx::Json json;
    std::string error;
    std::string trimmed = util::trim(raw);
    if (trimmed.empty()) throw std::runtime_error("课表数据为空。");
    if (!jsonx::Json::parse(trimmed, json, error)) throw std::runtime_error("课表 JSON 解析失败：" + error);
    if (!json.isArray()) throw std::runtime_error("课表接口返回的不是数组。");

    int firstDay = 0;
    if (!util::parseDateDays(semester.firstDay, firstDay)) throw std::runtime_error("学期第一天格式无效。");
    std::map<std::string, size_t> index;

    for (const jsonx::Json &item : json.array) {
        if (item.type != jsonx::Json::Type::Object) continue;
        std::string name = util::trim(item.getString("nr"));
        std::string location = util::trim(item.getString("dd"));
        std::string kind = util::trim(item.getString("fl"));
        std::string startTime = util::trim(item.getString("kssj"));
        std::string endTime = util::trim(item.getString("jssj"));
        int days = 0;
        if (!util::parseDateDays(item.getString("nq"), days)) {
            result.warnings.push_back("跳过日期无效的课表条目：" + name);
            continue;
        }
        int diff = days - firstDay;
        int week = (diff >= 0 ? diff / 7 : -((-diff + 6) / 7)) + 1;
        if (week < 1 || week > 30) {
            result.warnings.push_back("跳过不在学期范围内的课表条目：" + name + "（第 " + std::to_string(week) + " 周）");
            continue;
        }
        int dayOfWeek = weekdayFromDays(days);
        if (name.empty()) continue;

        if (kind == "上课" || kind == "实验") {
            int begin = 0, end = 0;
            if (!periodFromBegin(startTime, begin) || !periodFromEnd(endTime, end)) {
                result.warnings.push_back("跳过节次无法识别的课程：" + name + "（" + startTime + "-" + endTime + "）");
                continue;
            }
            if (end < begin) end = begin;
            Course &course = findOrCreate(result.courses, index, name, location, Type::Primary, name + "@" + location);
            TimeSlice slice;
            slice.dayOfWeek = dayOfWeek;
            slice.begin = begin;
            slice.end = end;
            slice.weeks = {week};
            addSlice(course, slice);
        } else if (kind == "考试") {
            int beginMinutes = 0, endMinutes = 0;
            if (!util::parseHhmm(startTime, beginMinutes) || !util::parseHhmm(endTime, endMinutes)) {
                result.warnings.push_back("跳过时间无法识别的考试：" + name);
                continue;
            }
            std::string examName = "[考试]" + name;
            Course &course = findOrCreate(result.courses, index, examName, location, Type::Exam, name + "@" + location);
            ExamSlice exam;
            exam.dayOfWeek = dayOfWeek;
            exam.weekNumber = week;
            exam.beginMinutes = beginMinutes;
            exam.endMinutes = endMinutes;
            course.exams.push_back(exam);
        }
    }
    return result;
}

ParseResult parseSecondaryHtml(const std::string &html) {
    ParseResult result;
    std::map<std::string, size_t> index;

    const int beginPeriods[6] = {1, 3, 6, 8, 10, 12};
    const int endPeriods[6] = {2, 5, 7, 9, 11, 14};
    const std::string marker = "onmouseout='return nd();'";
    const std::string overlib = "overlib('";

    size_t pos = 0;
    while ((pos = html.find(overlib, pos)) != std::string::npos) {
        size_t detailStart = pos + overlib.size();
        size_t detailEnd = html.find("');", detailStart);
        if (detailEnd == std::string::npos) break;
        std::string detail = jsUnescape(html.substr(detailStart, detailEnd - detailStart));
        size_t titleMarker = html.find(marker, detailEnd);
        if (titleMarker == std::string::npos) {
            pos = detailEnd + 3;
            continue;
        }
        size_t titleTagEnd = html.find('>', titleMarker + marker.size());
        if (titleTagEnd == std::string::npos) break;
        size_t titleStart = titleTagEnd + 1;
        size_t spanEnd = html.find("</span>", titleStart);
        if (spanEnd == std::string::npos) break;
        std::string title = util::trim(util::stripTags(jsUnescape(html.substr(titleStart, spanEnd - titleStart))));

        size_t idPos = html.find("getElementById('", spanEnd);
        if (idPos == std::string::npos) break;
        idPos += std::string("getElementById('").size();
        size_t idEnd = html.find("')", idPos);
        if (idEnd == std::string::npos) break;
        std::string basic = html.substr(idPos, idEnd - idPos);
        pos = idEnd + 2;

        if (basic.size() < 4) continue;
        if (!std::isdigit(static_cast<unsigned char>(basic[1])) ||
            !std::isdigit(static_cast<unsigned char>(basic[3]))) {
            continue;
        }
        int sessionIndex = basic[1] - '0';
        int dayOfWeek = basic[3] - '0';
        if (sessionIndex < 1 || sessionIndex > 6 || dayOfWeek < 1 || dayOfWeek > 7) continue;
        if (title.empty()) continue;

        std::string compact;
        for (char c : detail) {
            if (!std::isspace(static_cast<unsigned char>(c))) compact.push_back(c);
        }
        std::string location;
        size_t open = compact.find('(');
        if (open != std::string::npos) {
            size_t comma = compact.find("，", open + 1);
            if (comma == std::string::npos) comma = compact.find(',', open + 1);
            if (comma == std::string::npos) comma = compact.find(')', open + 1);
            if (comma != std::string::npos) location = compact.substr(open + 1, comma - open - 1);
        }

        std::vector<int> weeks = secondaryWeeks(compact);
        if (weeks.empty()) {
            result.warnings.push_back("跳过无法识别周次的二级课表条目：" + title);
            continue;
        }

        Course &course = findOrCreate(result.courses, index, title, location, Type::Secondary, title + "@" + location);
        TimeSlice slice;
        slice.dayOfWeek = dayOfWeek;
        slice.begin = beginPeriods[sessionIndex - 1];
        slice.end = endPeriods[sessionIndex - 1];
        slice.weeks = weeks;
        addSlice(course, slice);
    }
    return result;
}

std::vector<Course> mergeCourses(std::vector<Course> first, const std::vector<Course> &second) {
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < first.size(); ++i) index[keyFor(first[i])] = i;

    for (const Course &course : second) {
        auto it = index.find(keyFor(course));
        if (it == index.end()) {
            first.push_back(course);
            index[keyFor(course)] = first.size() - 1;
            continue;
        }
        Course &target = first[it->second];
        for (const TimeSlice &slice : course.slices) addSlice(target, slice);
        target.exams.insert(target.exams.end(), course.exams.begin(), course.exams.end());
    }
    return first;
}

std::string shortSemesterLabel(const std::string &semesterName) {
    // 年份：取第一段 4 位数字的后两位。学期名形如 "2026-2027学年秋季学期"。
    std::string year;
    for (size_t i = 0; i + 3 < semesterName.size(); ++i) {
        bool digits = true;
        for (size_t k = 0; k < 4; ++k) {
            char c = semesterName[i + k];
            if (c < '0' || c > '9') { digits = false; break; }
        }
        if (digits) {
            year = semesterName.substr(i + 2, 2);
            break;
        }
    }

    std::string season;
    if (semesterName.find("秋") != std::string::npos) season = "Fall";
    else if (semesterName.find("春") != std::string::npos) season = "Spring";
    else if (semesterName.find("夏") != std::string::npos) season = "Summer";
    else if (semesterName.find("冬") != std::string::npos) season = "Winter";

    if (season.empty()) {
        std::string lower = util::toLower(semesterName);
        if (lower.find("fall") != std::string::npos || lower.find("autumn") != std::string::npos) season = "Fall";
        else if (lower.find("spring") != std::string::npos) season = "Spring";
        else if (lower.find("summer") != std::string::npos) season = "Summer";
        else if (lower.find("winter") != std::string::npos) season = "Winter";
    }

    return year + season;
}

} // namespace schedule
