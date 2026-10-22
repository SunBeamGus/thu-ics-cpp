#pragma once

#include "http.h"

#include <cstdint>
#include <string>
#include <vector>

namespace thu {

struct Semester {
    std::string firstDay; // YYYY-MM-DD
    std::string semesterId;
    std::string semesterName;
    int weekCount = 0;
};

struct CalendarData {
    Semester current;
    std::vector<Semester> next;
};

struct Options {
    std::string userId;
    std::string password;
    std::string fingerprint;
    std::string proxy;
    bool trustDevice = true;
    bool verbose = false;
};

class Session {
   public:
    explicit Session(Options options);

    void login();

    // Raw JSON from the semester-list API.
    std::string fetchCalendarRaw();
    // Raw JSON array merged from all jxrl pages (without the JSONP wrapper).
    std::string fetchPrimaryRaw(const Semester &semester);
    // Raw HTML of the "secondary" timetable page (本科生 only).
    std::string fetchSecondaryRaw();

    bool graduate() const;
    const Options &options() const { return options_; }
    void log(const std::string &message) const;

   private:
    std::string idLogin(const std::string &sm2PublicKey);
    std::string promptTotpCode();
    std::string handleTwoFactor();
    std::string roamId(const std::string &payload);
    std::string roamDefault(const std::string &payload);
    void relogin();

    std::string callbackUrl(const std::string &html) const;
    std::string errorMessage(const std::string &html) const;

    Options options_;
    http::HttpClient http_;
    bool loggedIn_ = false;
};

std::string extractSm2PublicKey(const std::string &html);
std::string extractLoginError(const std::string &html);
// Returns the 6 digits contained in `input`, ignoring spaces/tabs/dashes, or an
// empty string if the input is not exactly six digits.
std::string normalizeTotpCode(const std::string &input);

} // namespace thu
