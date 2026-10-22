#pragma once

#include "thu.h"

#include <string>
#include <vector>

namespace schedule {

enum class Type { Primary, Secondary, Exam };

struct TimeSlice {
    int dayOfWeek = 1; // 1 = Monday ... 7 = Sunday
    int begin = 1;     // period index 1..14
    int end = 1;
    std::vector<int> weeks; // 1-based
};

struct ExamSlice {
    int dayOfWeek = 1;
    int weekNumber = 1;
    int beginMinutes = 0; // minutes after midnight, Beijing time
    int endMinutes = 0;
};

struct Course {
    std::string name;
    std::string location;
    std::string hash;
    Type type = Type::Primary;
    std::vector<TimeSlice> slices;
    std::vector<ExamSlice> exams;
};

struct ParseResult {
    std::vector<Course> courses;
    std::vector<std::string> warnings;
};

thu::CalendarData parseCalendarJson(const std::string &raw);
ParseResult parsePrimaryJson(const std::string &raw, const thu::Semester &semester);
ParseResult parseSecondaryHtml(const std::string &html);
std::vector<Course> mergeCourses(std::vector<Course> first, const std::vector<Course> &second);

int periodBeginMinutes(int period);
int periodEndMinutes(int period);
bool periodFromBegin(const std::string &hhmm, int &period);
bool periodFromEnd(const std::string &hhmm, int &period);

} // namespace schedule
