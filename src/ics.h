#pragma once

#include "schedule.h"
#include "thu.h"

#include <string>
#include <vector>

namespace ics {

struct Event {
    std::string uid;
    std::string dtStart;
    std::string dtEnd;
    std::string summary;
    std::string location;
    std::string description;
};

std::vector<Event> buildEvents(const std::vector<schedule::Course> &courses, const thu::Semester &semester);
std::string generate(const std::vector<Event> &events, const std::string &calendarName,
                     const std::string &calendarDescription);

} // namespace ics
