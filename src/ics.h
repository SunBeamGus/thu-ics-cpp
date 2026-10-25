#pragma once

#include "location.h"
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
    std::string geoLat; // 有坐标时写成 GEO 与 X-APPLE-STRUCTURED-LOCATION
    std::string geoLon;
    std::string description;
};

// resolver 为 nullptr 时直接把 course.location 原样写进 LOCATION。
std::vector<Event> buildEvents(const std::vector<schedule::Course> &courses, const thu::Semester &semester,
                               location::Resolver *resolver = nullptr);
std::string generate(const std::vector<Event> &events, const std::string &calendarName,
                     const std::string &calendarDescription);

} // namespace ics
