#include "selftest.h"

#include "ics.h"
#include "json.h"
#include "schedule.h"
#include "sm2.h"
#include "thu.h"
#include "sm3.h"
#include "util.h"

#include <cstdio>
#include <string>
#include <vector>

namespace selftest {
namespace {

int gFailures = 0;

void check(bool condition, const std::string &name, const std::string &detail = "") {
    if (condition) {
        std::printf("  [ok] %s\n", name.c_str());
    } else {
        ++gFailures;
        std::printf("  [FAIL] %s%s%s\n", name.c_str(), detail.empty() ? "" : ": ", detail.c_str());
    }
}

void checkEqual(const std::string &actual, const std::string &expected, const std::string &name) {
    check(actual == expected, name, "got '" + actual + "', expected '" + expected + "'");
}

void testCrypto() {
    std::printf("crypto:\n");
    checkEqual(crypto::sm3Hex(std::string("abc")),
               "66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0", "SM3(\"abc\")");
    checkEqual(crypto::sm3Hex(std::string("")),
               "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b", "SM3(\"\")");

    std::string error;
    check(crypto::sm2SelfCheck(error), "SM2 encrypt/decrypt + standard vectors", error);
}

void testJson() {
    std::printf("json:\n");
    jsonx::Json json;
    std::string error;
    bool ok = jsonx::Json::parse(R"({"a":[1,2,{"b":"\u4e2d\u6587"}],"c":true,"d":null})", json, error);
    check(ok, "parse nested JSON", error);
    const jsonx::Json *a = json.find("a");
    check(a != nullptr && a->isArray() && a->array.size() == 3, "array size");
    if (a && a->array.size() == 3) {
        check(a->array[2].getString("b") == "中文", "\\u escape decoding");
    }
    check(json.getInt("c", 0) == 0 && json.find("c")->boolean, "boolean parsing");
    check(json.find("d")->isNull(), "null parsing");
}

void testDates() {
    std::printf("dates:\n");
    check(util::daysFromCivil(1970, 1, 1) == 0, "epoch");
    check(util::formatDate(0) == "1970-01-01", "formatDate(0)");
    int days = 0;
    check(util::parseDateDays("2026-09-14 08:00:00", days), "parseDateDays");
    check(util::formatDate(days) == "2026-09-14", "parse/format round trip");
    int y, m, d;
    util::civilFromDays(days, y, m, d);
    check(y == 2026 && m == 9 && d == 14, "civilFromDays");
    int minutes = 0;
    check(util::parseHhmm("08:05", minutes) && minutes == 485, "parseHhmm");
    check(util::parseHhmm("08：05", minutes) && minutes == 485, "parseHhmm full-width colon");
}

void testLoginPageParsing() {
    std::printf("login page parsing:\n");
    const std::string key =
        "04d0c9e1ae89279fe05b435d63e3eba437bf510e09da5f71558974a19dc596724227f08dc2fc6e74bbb9d8b468d4dd5205e9b6793a3bbc48df3fdf219b3ea140e3";
    const std::string html =
        "<script>var publicKey = $('#sm2publicKey').text();</script>"
        "<div style=\"display:none\" id=\"sm2publicKey\">" +
        key + "</div>";
    checkEqual(thu::extractSm2PublicKey(html), key, "extract #sm2publicKey with decoy in script");
    check(thu::extractSm2PublicKey("<html>no key</html>").empty(), "missing key");
    const std::string errorHtml =
        "<script>$('#msg_note').html('请输入用户名');</script>"
        "<span id=\"msg_note\" class=\"red\">\n 您的用户名或密码不正确，请重试！\n</span>";
    checkEqual(thu::extractLoginError(errorHtml), "您的用户名或密码不正确，请重试！", "extract login error");

    checkEqual(thu::normalizeTotpCode("123456"), "123456", "TOTP input");
    checkEqual(thu::normalizeTotpCode(" 123 456 "), "123456", "TOTP input with spaces");
    checkEqual(thu::normalizeTotpCode("123-456"), "123456", "TOTP input with dash");
    check(thu::normalizeTotpCode("12345").empty(), "TOTP input too short");
    check(thu::normalizeTotpCode("12a456").empty(), "TOTP input non-digit");

    check(thu::parseTrustDevice("1") == thu::TrustDevice::Yes, "trust device: 1");
    check(thu::parseTrustDevice(" YES ") == thu::TrustDevice::Yes, "trust device: YES");
    check(thu::parseTrustDevice("true") == thu::TrustDevice::Yes, "trust device: true");
    check(thu::parseTrustDevice("no") == thu::TrustDevice::No, "trust device: no");
    check(thu::parseTrustDevice("0") == thu::TrustDevice::No, "trust device: 0");
    check(thu::parseTrustDevice("Never") == thu::TrustDevice::No, "trust device: Never");
    check(thu::parseTrustDevice("ask") == thu::TrustDevice::Ask, "trust device: ask");
    check(thu::parseTrustDevice("") == thu::TrustDevice::Ask, "trust device: empty");
}

void testTotpWindow() {
    std::printf("totp window:\n");
    check(thu::totpWindowEndsAt(0) == 30, "window ends at 30 (t=0)");
    check(thu::totpWindowEndsAt(29) == 30, "window ends at 30 (t=29)");
    check(thu::totpWindowEndsAt(30) == 60, "window ends at 60 (t=30)");
    check(thu::totpWindowEndsAt(31) == 60, "window ends at 60 (t=31)");
    const long long now = 1730000000;
    const long long end = thu::totpWindowEndsAt(now);
    check(end > now, "window end is in the future");
    check(end - now <= 30, "window end at most one step away");
    check(thu::totpWindowEndsAt(59, 60) == 60, "custom step 60");
    check(thu::totpWindowEndsAt(60, 0) == 90, "invalid step falls back to 30");
}

void testScheduleParsing() {
    std::printf("schedule parsing:\n");
    const std::string calendarRaw =
        R"({"message":"success","result":{"kssj":"2026-09-12 00:00:00","jssj":"2027-01-08 00:00:00","id":"2026-2027-1","xnxqmc":"2026-2027学年秋季学期"},"resultList":[{"kssj":"2027-02-20 00:00:00","jssj":"2027-06-18 00:00:00","id":"2026-2027-2","xnxqmc":"2026-2027学年春季学期"}]})";
    thu::CalendarData calendar;
    bool threw = false;
    try {
        calendar = schedule::parseCalendarJson(calendarRaw);
    } catch (const std::exception &e) {
        threw = true;
        std::printf("    exception: %s\n", e.what());
    }
    check(!threw, "parse calendar json");
    check(calendar.current.firstDay == "2026-09-14", "firstDay = kssj + 2 days",
          calendar.current.firstDay);
    check(calendar.next.size() == 1 && calendar.next[0].firstDay == "2027-02-22", "next semester firstDay");

    const std::string primaryRaw = R"([
        {"nq":"2026-09-14 08:00:00","nr":"微积分","dd":"三教1101","fl":"上课","kssj":"08:00","jssj":"09:35"},
        {"nq":"2026-09-21 08:00:00","nr":"微积分","dd":"三教1101","fl":"上课","kssj":"08:00","jssj":"09:35"},
        {"nq":"2026-09-15 19:20:00","nr":"大学物理","dd":"六教6A","fl":"实验","kssj":"19:20","jssj":"20:55"},
        {"nq":"2027-01-05 09:00:00","nr":"微积分","dd":"三教1101","fl":"考试","kssj":"09:00","jssj":"11:00"}
    ])";
    schedule::ParseResult parsed = schedule::parsePrimaryJson(primaryRaw, calendar.current);
    check(parsed.courses.size() == 3, "primary course count", std::to_string(parsed.courses.size()));
    const schedule::Course *calculus = nullptr;
    for (const auto &course : parsed.courses) {
        if (course.name == "微积分") calculus = &course;
    }
    check(calculus != nullptr && calculus->slices.size() == 1 && calculus->slices[0].weeks.size() == 2,
          "merge same week day block");
    if (calculus) {
        check(calculus->slices[0].dayOfWeek == 1 && calculus->slices[0].begin == 1 && calculus->slices[0].end == 2,
              "period mapping 08:00-09:35 -> 1-2");
    }

    const std::string secondary = R"(strHTML = "<span onmouseover=\"return overlib('文化素质(六教B1，张老师)第1-4周');\" onmouseout='return nd();'>文化素质</span>"; document.getElementById('k1b3').innerHTML += strHTML+"<br>";)";
    schedule::ParseResult secondaryParsed = schedule::parseSecondaryHtml(secondary);
    check(secondaryParsed.courses.size() == 1, "secondary course count");
    if (!secondaryParsed.courses.empty()) {
        const auto &course = secondaryParsed.courses[0];
        check(course.location == "六教B1", "secondary location", course.location);
        check(course.slices.size() == 1 && course.slices[0].weeks == std::vector<int>({1, 2, 3, 4}),
              "secondary weeks");
        check(course.slices[0].dayOfWeek == 3 && course.slices[0].begin == 1 && course.slices[0].end == 2,
              "secondary day/period");
    }
}

void testIcs() {
    std::printf("ics:\n");
    thu::CalendarData calendar;
    calendar.current.firstDay = "2026-09-14";
    calendar.current.semesterName = "2026-2027学年秋季学期";
    calendar.current.weekCount = 16;

    schedule::Course course;
    course.name = "微积分";
    course.location = "三教1101";
    schedule::TimeSlice slice;
    slice.dayOfWeek = 1;
    slice.begin = 1;
    slice.end = 2;
    slice.weeks = {1, 2};
    course.slices.push_back(slice);

    std::vector<ics::Event> events = ics::buildEvents({course}, calendar.current);
    check(events.size() == 2, "event count");
    if (!events.empty()) {
        checkEqual(events[0].dtStart, "20260914T000000Z", "Beijing 08:00 -> UTC 00:00");
        checkEqual(events[0].dtEnd, "20260914T013500Z", "Beijing 09:35 -> UTC 01:35");
    }
    std::string text = ics::generate(events, "清华大学课表（测试）", "测试日历");
    check(text.find("\r\nEND:VCALENDAR\r\n") != std::string::npos, "CRLF endings");
    check(text.find("SUMMARY:微积分\r\n") != std::string::npos, "summary");
    check(text.find("UID:course-") != std::string::npos, "stable uid");

    std::string longLine = "SUMMARY:" + std::string(120, 'x');
    std::string folded = util::foldIcsLine(longLine);
    bool ok = true;
    size_t pos = 0;
    while (pos <= folded.size()) {
        size_t eol = folded.find("\r\n", pos);
        if (eol == std::string::npos) eol = folded.size();
        if (eol - pos > 75) {
            ok = false;
            break;
        }
        if (eol == folded.size()) break;
        pos = eol + 2;
        if (pos < folded.size() && folded[pos] != ' ') {
            ok = false;
            break;
        }
    }
    check(ok, "line folding <= 75 octets");
}

} // namespace

bool run(std::string &report) {
    gFailures = 0;
    std::printf("thu-courses-ics self tests\n");
    testCrypto();
    testJson();
    testDates();
    testLoginPageParsing();
    testTotpWindow();
    testScheduleParsing();
    testIcs();
    if (gFailures == 0) {
        report = "all checks passed";
        std::printf("all checks passed\n");
        return true;
    }
    report = std::to_string(gFailures) + " check(s) failed";
    std::printf("%d check(s) failed\n", gFailures);
    return false;
}

} // namespace selftest
