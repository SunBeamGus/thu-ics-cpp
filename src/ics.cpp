#include "ics.h"

#include "util.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace ics {
namespace {

const char *kDayNames[8] = {"", "周一", "周二", "周三", "周四", "周五", "周六", "周日"};

int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

std::string utcIcs(int days, int minutes) {
    int total = days * 1440 + minutes - 8 * 60; // Beijing time (UTC+8) -> UTC
    int day = floorDiv(total, 1440);
    int minuteOfDay = total - day * 1440;
    int hour = minuteOfDay / 60;
    int minute = minuteOfDay % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%sT%02d%02d00Z", util::formatCompact(day).c_str(), hour, minute);
    return buf;
}

std::string escapeIcs(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case ';': out += "\\;"; break;
            case ',': out += "\\,"; break;
            case '\r': break;
            case '\n': out += "\\n"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

uint64_t fnv1a(const std::string &s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string courseKey(const schedule::Course &course) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(fnv1a(course.name + "\x1f" + course.location)));
    return buf;
}

} // namespace

std::vector<Event> buildEvents(const std::vector<schedule::Course> &courses, const thu::Semester &semester) {
    std::vector<Event> events;
    int firstDay = 0;
    if (!util::parseDateDays(semester.firstDay, firstDay)) return events;

    for (const schedule::Course &course : courses) {
        const std::string key = courseKey(course);
        for (const schedule::TimeSlice &slice : course.slices) {
            int beginMinutes = schedule::periodBeginMinutes(slice.begin);
            int endMinutes = schedule::periodEndMinutes(slice.end);
            for (int week : slice.weeks) {
                if (week < 1) continue;
                int days = firstDay + (week - 1) * 7 + (slice.dayOfWeek - 1);
                Event event;
                char uid[128];
                std::snprintf(uid, sizeof(uid), "course-%s-w%d-d%d-b%d@thu-courses-auto", key.c_str(), week,
                              slice.dayOfWeek, slice.begin);
                event.uid = uid;
                event.dtStart = utcIcs(days, beginMinutes);
                event.dtEnd = utcIcs(days, endMinutes);
                event.summary = course.name;
                event.location = course.location;
                event.description = "第" + std::to_string(week) + "周 " + kDayNames[slice.dayOfWeek] + " 第" +
                                    std::to_string(slice.begin) + "-" + std::to_string(slice.end) + "节";
                events.push_back(std::move(event));
            }
        }
        for (const schedule::ExamSlice &exam : course.exams) {
            int endMinutes = exam.endMinutes;
            if (endMinutes <= exam.beginMinutes) endMinutes = exam.beginMinutes + 45;
            int days = firstDay + (exam.weekNumber - 1) * 7 + (exam.dayOfWeek - 1);
            Event event;
            char uid[128];
            std::snprintf(uid, sizeof(uid), "course-%s-exam-w%d-d%d@thu-courses-auto", key.c_str(), exam.weekNumber,
                          exam.dayOfWeek);
            event.uid = uid;
            event.dtStart = utcIcs(days, exam.beginMinutes);
            event.dtEnd = utcIcs(days, endMinutes);
            event.summary = course.name;
            event.location = course.location;
            char timeText[16];
            std::snprintf(timeText, sizeof(timeText), "%02d:%02d-%02d:%02d", exam.beginMinutes / 60,
                          exam.beginMinutes % 60, endMinutes / 60, endMinutes % 60);
            event.description =
                "第" + std::to_string(exam.weekNumber) + "周 " + kDayNames[exam.dayOfWeek] + " 考试 " + timeText;
            events.push_back(std::move(event));
        }
    }

    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        if (a.dtStart != b.dtStart) return a.dtStart < b.dtStart;
        if (a.summary != b.summary) return a.summary < b.summary;
        return a.uid < b.uid;
    });
    return events;
}

std::string generate(const std::vector<Event> &events, const std::string &calendarName,
                     const std::string &calendarDescription) {
    const std::string dtstamp = util::nowUtcIcs();
    std::vector<std::string> lines;
    lines.push_back("BEGIN:VCALENDAR");
    lines.push_back("VERSION:2.0");
    lines.push_back("PRODID:-//thu-courses-auto//THU Schedule ICS//CN");
    lines.push_back("CALSCALE:GREGORIAN");
    lines.push_back("METHOD:PUBLISH");
    lines.push_back("X-WR-CALNAME:" + escapeIcs(calendarName));
    lines.push_back("X-WR-CALDESC:" + escapeIcs(calendarDescription));
    lines.push_back("X-WR-TIMEZONE:Asia/Shanghai");
    lines.push_back("X-PUBLISHED-TTL:PT1H");

    for (const Event &event : events) {
        lines.push_back("BEGIN:VEVENT");
        lines.push_back("UID:" + event.uid);
        lines.push_back("DTSTAMP:" + dtstamp);
        lines.push_back("DTSTART:" + event.dtStart);
        lines.push_back("DTEND:" + event.dtEnd);
        lines.push_back("SUMMARY:" + escapeIcs(event.summary));
        if (!event.location.empty()) lines.push_back("LOCATION:" + escapeIcs(event.location));
        if (!event.description.empty()) lines.push_back("DESCRIPTION:" + escapeIcs(event.description));
        lines.push_back("END:VEVENT");
    }
    lines.push_back("END:VCALENDAR");

    std::string out;
    for (const std::string &line : lines) {
        out += util::foldIcsLine(line);
        out += "\r\n";
    }
    return out;
}

} // namespace ics
