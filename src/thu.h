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

struct Account {
    std::string userId;
    std::string password;
};

// 解析账号列表文本，用于批量生成：# 开头与空行忽略；每行 "学号<分隔>密码"，
// 分隔符可以是 , ; : = | / 或空白，密码里可以继续包含这些字符（只按第一个分开）。
// 无法解析或重复的行会追加到 problems（可为 nullptr），不会中断解析。
std::vector<Account> parseAccounts(const std::string &text, std::vector<std::string> *problems);

// 是否在清华 ID 内信任本设备。信任后本机后续登录通常可跳过两步验证；
// Ask 表示等真正触发两步验证时再询问用户。
enum class TrustDevice { Ask, No, Yes };

struct Options {
    std::string userId;
    std::string password;
    std::string fingerprint;
    std::string proxy;
    TrustDevice trustDevice = TrustDevice::Ask;
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
    bool askTrustDevice();
    std::string handleTwoFactor();
    std::string roamId(const std::string &payload);
    std::string roamDefault(const std::string &payload);
    void relogin();

    std::string callbackUrl(const std::string &html) const;
    std::string errorMessage(const std::string &html) const;

    Options options_;
    http::HttpClient http_;
    bool loggedIn_ = false;

    // 上一次被服务器接受过的 TOTP 验证码，以及它所在的 30 秒窗口的结束时刻
    // （epoch 秒）。同一次运行里再次触发两步验证时，窗口内直接复用、不再询问。
    std::string lastTotpCode_;
    long long lastTotpValidUntil_ = 0;
};

std::string extractSm2PublicKey(const std::string &html);
std::string extractLoginError(const std::string &html);
// Returns the 6 digits contained in `input`, ignoring spaces/tabs/dashes, or an
// empty string if the input is not exactly six digits.
std::string normalizeTotpCode(const std::string &input);
// "1"/"y"/"yes"/"true"/"on" -> Yes, "0"/"n"/"no"/"false"/"off" -> No,
// anything else (including empty and "ask") -> Ask.
TrustDevice parseTrustDevice(const std::string &text);

// End of the TOTP window that contains `nowSeconds`: the code is valid until
// then, so it does not have to be asked for again inside the same window.
// `step` is the TOTP period in seconds (30 by default).
long long totpWindowEndsAt(long long nowSeconds, int step = 30);

// 由学号派生出的公开代号（文件名用它，避免把学号写进 URL）。
// 同一个 salt + 学号永远得到同一个 16 位十六进制代号；换 salt 会得到不同的代号，
// 因此 salt 要保存在本地、不要公开，别人就拿不到学号 ↔ 代号的对应关系。
std::string deriveIdCode(const std::string &salt, const std::string &userId);

} // namespace thu
