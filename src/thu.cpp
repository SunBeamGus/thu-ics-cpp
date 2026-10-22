#include "thu.h"

#include "json.h"
#include "sm2.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>

namespace thu {
namespace {

// Keep in sync with thu-info-lib src/constants/strings.ts
const char *kWebVpnOauthLoginUrl = "https://webvpn.tsinghua.edu.cn/login?oauth_login=true";
const char *kLoginUrl = "https://webvpn.tsinghua.edu.cn/login";
const char *kIdHostUrl = "https://id.tsinghua.edu.cn";
const char *kIdBaseUrl = "https://id.tsinghua.edu.cn/do/off/ui/auth/login/form/";
const char *kIdLoginUrl = "https://id.tsinghua.edu.cn/do/off/ui/auth/login/check";
const char *kDoubleAuthUrl = "https://id.tsinghua.edu.cn/b/doubleAuth/login";
const char *kSaveFingerUrl = "https://id.tsinghua.edu.cn/b/doubleAuth/personal/saveFinger";
const char *kGetCookieUrl =
    "https://webvpn.tsinghua.edu.cn/wengine-vpn/cookie?method=get&host=info.tsinghua.edu.cn&scheme=https&path=/f/"
    "info/gxfw_fg/common/index";
const char *kRoamingUrl =
    "https://webvpn.tsinghua.edu.cn/https/"
    "77726476706e69737468656265737421f9f9479369247b59700f81b9991b2631506205de/b/yyfw/vyyfwxx/info/portal_fg/"
    "common/onlineAppRedirect";
const char *kLearnHomeUrl =
    "https://webvpn.tsinghua.edu.cn/https/"
    "77726476706e69737468656265737421fcf2408e297e7c4377068ea48d546d30ca8cc97bcc/f/wlxt/index/course/student/"
    "index";
const char *kSemesterListUrl =
    "https://webvpn.tsinghua.edu.cn/https/"
    "77726476706e69737468656265737421fcf2408e297e7c4377068ea48d546d30ca8cc97bcc/b/kc/zhjw_v_code_xnxq/"
    "getCurrentAndNextSemester?_csrf=";
const char *kJxrlBksPrefix =
    "https://webvpn.tsinghua.edu.cn/http/"
    "77726476706e69737468656265737421eaff4b8b69336153301c9aa596522b20bc86e6e559a9b290/jxmh_out.do?m="
    "bks_jxrl_all&p_start_date=";
const char *kJxrlYjsPrefix =
    "https://webvpn.tsinghua.edu.cn/http/"
    "77726476706e69737468656265737421eaff4b8b69336153301c9aa596522b20bc86e6e559a9b290/jxmh_out.do?m="
    "yjs_jxrl_all&p_start_date=";
const char *kJxrlMiddle = "&p_end_date=";
const char *kJxrlSuffix = "&jsoncallback=m";
const char *kSecondaryUrl =
    "https://webvpn.tsinghua.edu.cn/http/"
    "77726476706e69737468656265737421eaff4b8b69336153301c9aa596522b20bc86e6e559a9b290/portal3rd.do?m="
    "bks_ejkbSearch";

const char *kLearnRoamPayload = "3E401364BDD7AEA7EBF1EDE3F15ED4B7";
const char *kScheduleRoamPayloadBks = "287C0C6D90ABB364CD5FDF1495199962";
const char *kScheduleRoamPayloadYjs = "BEABB32641DC4EC3510B048BAF42471A";
const char *kIdRoamPayload = "10000ea055dd8d81d09d5a1ba55d39ad";

const std::map<std::string, std::string> kHostMap = {
    {"info", "77726476706e69737468656265737421f9f9479369247b59700f81b9991b2631506205de"},
    {"zhjw.cic", "77726476706e69737468656265737421eaff4b8b69336153301c9aa596522b20bc86e6e559a9b290"},
    {"jxgl.cic", "77726476706e69737468656265737421faef469069336153301c9aa596522b20e33c1eb39606919f"},
    {"zhjwxk.cic", "77726476706e69737468656265737421faef469069336153301c9aa596522b20e33c1eb39606919f"},
    {"ecard", "77726476706e69737468656265737421f5f4408e237e7c4377068ea48d546d303341e9882a"},
    {"learn", "77726476706e69737468656265737421fcf2408e297e7c4377068ea48d546d30ca8cc97bcc"},
    {"mails", "77726476706e69737468656265737421fdf64890347e7c4377068ea48d546d3011ff591d40"},
    {"50", "77726476706e69737468656265737421a5a70f8834396657761d88e29d51367b6a00"},
    {"166.111.14.8", "77726476706e69737468656265737421a1a117d27661391e2f5cc7f4"},
    {"fa-online", "77726476706e69737468656265737421f6f60c93293c615e7b469dbf915b243daf0f96e17deaf447b4"},
    {"dzpj", "77726476706e69737468656265737421f4ed519669247b59700f81b9991b2631aee63c51"},
    {"jjhyhdf", "77726476706e69737468656265737421fafd49852f346e1e6a1b80a29f5d36342bb9c40cf69277"},
    {"yhdf", "77726476706e69737468656265737421e9ff459a69247b59700f81b9991b26317dbd36ae"},
    {"usereg", "77726476706e69737468656265737421e5e4448e223726446d0187ab9040227b54b6c80fcd73"},
    {"thos", "77726476706e69737468656265737421e4ff4e8f69247b59700f81b9991b2631ca359dd4"},
    {"zzjl.graduate", "77726476706e69737468656265737421eaed4b9069377a517a1d88b89d1b37269c624d2b1c6925f37faea82b8d"},
    {"madmodel.cs", "77726476706e69737468656265737421fdf6459128346d5c300b9ae28c462a3b27469fc32211fa26a3e464"},
};

std::string getWebVpnUrl(const std::string &urlIn) {
    if (urlIn.find("oauth.tsinghua.edu.cn") != std::string::npos) return urlIn;
    size_t schemeEnd = urlIn.find("://");
    if (schemeEnd == std::string::npos) return urlIn;
    std::string scheme = urlIn.substr(0, schemeEnd);
    std::string rest = urlIn.substr(schemeEnd + 3);
    size_t hostEnd = rest.find_first_of(":/?#");
    std::string host = rest.substr(0, hostEnd);
    std::string port = scheme == "https" ? "443" : "80";
    size_t pathStart = rest.find_first_of("/?#");
    if (hostEnd != std::string::npos && rest[hostEnd] == ':') {
        size_t colon = hostEnd;
        size_t after = rest.find_first_of("/?#", colon + 1);
        port = rest.substr(colon + 1, (after == std::string::npos ? rest.size() : after) - colon - 1);
    }
    std::string uri = pathStart == std::string::npos ? "/" : rest.substr(pathStart);
    return "https://oauth.tsinghua.edu.cn/lb-auth/lbredirect?scheme=" + scheme + "&host=" + host + "&port=" + port +
           "&uri=" + uri;
}

std::string absoluteUrl(const std::string &url) {
    if (url.empty()) return url;
    if (util::startsWith(url, "//")) return "https:" + url;
    if (url[0] == '/') return std::string(kIdHostUrl) + url;
    return url;
}

std::string parseRoamingUrl(const std::string &urlIn) {
    static const std::regex ipRe(R"(^http://(\d+\.\d+\.\d+\.\d+):(\d+)/(.+)$)");
    static const std::regex hostRe(R"(^(https?)://(.+?)\.tsinghua\.edu\.cn(?::(\d+))?/(.+)$)");
    std::smatch m;
    if (std::regex_match(urlIn, m, ipRe)) {
        auto it = kHostMap.find(m[1].str());
        if (it == kHostMap.end()) return urlIn;
        return "https://webvpn.tsinghua.edu.cn/http-" + m[2].str() + "/" + it->second + "/" + m[3].str();
    }
    if (std::regex_match(urlIn, m, hostRe)) {
        std::string host = m[2].str();
        if (host == "webvpn") return urlIn;
        auto it = kHostMap.find(host);
        if (it == kHostMap.end()) return urlIn;
        std::string protocol = m[1].str();
        if (m[3].matched) protocol += "-" + m[3].str();
        return "https://webvpn.tsinghua.edu.cn/" + protocol + "/" + it->second + "/" + m[4].str();
    }
    return urlIn;
}

bool isHexString(const std::string &s) {
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return !s.empty();
}

} // namespace

std::string normalizeTotpCode(const std::string &input) {
    std::string digits;
    for (char c : util::trim(input)) {
        if (c >= '0' && c <= '9') {
            digits.push_back(c);
        } else if (c == ' ' || c == '\t' || c == '-') {
            continue;
        } else {
            return "";
        }
    }
    return digits.size() == 6 ? digits : "";
}

TrustDevice parseTrustDevice(const std::string &text) {
    std::string value = util::toLower(util::trim(text));
    if (value == "1" || value == "y" || value == "yes" || value == "true" || value == "on" || value == "trust") {
        return TrustDevice::Yes;
    }
    if (value == "0" || value == "n" || value == "no" || value == "false" || value == "off" || value == "never") {
        return TrustDevice::No;
    }
    return TrustDevice::Ask;
}

std::string extractSm2PublicKey(const std::string &html) {
    size_t pos = 0;
    while ((pos = html.find("sm2publicKey", pos)) != std::string::npos) {
        size_t tagEnd = html.find('>', pos);
        size_t tagStart = html.rfind('<', pos);
        if (tagEnd != std::string::npos && tagStart != std::string::npos) {
            std::string tag = html.substr(tagStart, tagEnd - tagStart + 1);
            size_t valuePos = tag.find("value=");
            if (valuePos != std::string::npos) {
                size_t quote = tag.find_first_of("\"'", valuePos);
                if (quote != std::string::npos) {
                    size_t endQuote = tag.find(tag[quote], quote + 1);
                    if (endQuote != std::string::npos) {
                        std::string value = tag.substr(quote + 1, endQuote - quote - 1);
                        value = util::trim(value);
                        if ((value.size() == 128 || value.size() == 130) && isHexString(value)) return value;
                    }
                }
            }
            size_t contentEnd = html.find('<', tagEnd + 1);
            if (contentEnd != std::string::npos) {
                std::string text = html.substr(tagEnd + 1, contentEnd - tagEnd - 1);
                text.erase(std::remove_if(text.begin(), text.end(),
                                          [](unsigned char c) { return std::isspace(c) != 0; }),
                           text.end());
                if ((text.size() == 128 || text.size() == 130) && isHexString(text)) return text;
            }
        }
        pos += 12;
    }
    return "";
}

Session::Session(Options options) : options_(std::move(options)), http_(options_.proxy) {}

void Session::log(const std::string &message) const {
    if (options_.verbose) std::fprintf(stderr, "[thu] %s\n", message.c_str());
}

bool Session::graduate() const {
    return options_.userId.size() > 4 && (options_.userId[4] == '2' || options_.userId[4] == '3');
}

std::string Session::errorMessage(const std::string &html) const { return thu::extractLoginError(html); }

std::string extractLoginError(const std::string &html) {
    size_t pos = html.find("id=\"msg_note\"");
    if (pos == std::string::npos) pos = html.find("id='msg_note'");
    if (pos == std::string::npos) pos = html.find("id=msg_note");
    if (pos != std::string::npos) {
        size_t tagEnd = html.find('>', pos);
        size_t contentEnd = tagEnd == std::string::npos ? std::string::npos : html.find('<', tagEnd + 1);
        if (tagEnd != std::string::npos && contentEnd != std::string::npos) {
            std::string text = util::trim(util::stripTags(html.substr(tagEnd + 1, contentEnd - tagEnd - 1)));
            if (!text.empty()) return text;
        }
    }
    std::string text = util::trim(util::stripTags(html));
    if (text.size() > 300) text = text.substr(0, 300) + "...";
    return text.empty() ? "未知错误" : text;
}

std::string Session::callbackUrl(const std::string &html) const {
    size_t pos = 0;
    while ((pos = html.find("<a", pos)) != std::string::npos) {
        size_t tagEnd = html.find('>', pos);
        if (tagEnd == std::string::npos) break;
        std::string tag = html.substr(pos, tagEnd - pos + 1);
        size_t href = tag.find("href=");
        if (href == std::string::npos) href = tag.find("HREF=");
        if (href != std::string::npos) {
            size_t quote = tag.find_first_of("\"'", href);
            if (quote != std::string::npos) {
                size_t endQuote = tag.find(tag[quote], quote + 1);
                if (endQuote != std::string::npos) {
                    std::string url = tag.substr(quote + 1, endQuote - quote - 1);
                    return util::htmlDecode(util::trim(url));
                }
            }
        }
        pos = tagEnd + 1;
    }
    return "";
}

std::string Session::promptTotpCode() {
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::printf("🔑 请输入身份验证器 App 中的 6 位 TOTP 验证码（输入 q 取消）：");
        std::fflush(stdout);
        std::string line;
        if (!std::getline(std::cin, line)) {
            throw std::runtime_error("需要手动输入 TOTP 验证码，但当前没有可用的标准输入；请在交互式终端中运行。");
        }
        line = util::trim(line);
        if (line == "q" || line == "Q") throw std::runtime_error("已取消 TOTP 验证。");
        std::string digits = normalizeTotpCode(line);
        if (!digits.empty()) return digits;
        std::printf("验证码应为 6 位数字，请重新输入。\n");
    }
    throw std::runtime_error("未输入有效的 6 位 TOTP 验证码。");
}

// 询问用户是否在清华 ID 内信任本设备。默认“不信任”（直接回车不会把设备登记为可信）。
bool Session::askTrustDevice() {
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::printf("🔒 是否在清华 ID 内信任本设备？\n"
                    "   y = 信任：本机以后的登录通常不再要求验证码（可在清华 ID 的设备管理中移除）\n"
                    "   n = 不信任：每次都要求输入 TOTP 验证码\n"
                    "   请选择 [y/N]：");
        std::fflush(stdout);
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::printf("\n没有可用的标准输入，默认不信任本设备。\n");
            return false;
        }
        line = util::toLower(util::trim(line));
        if (line.empty() || line == "n" || line == "no" || line == "否") return false;
        if (line == "y" || line == "yes" || line == "是") return true;
        std::printf("请输入 y 或 n。\n");
    }
    std::printf("没有收到有效回答，默认不信任本设备。\n");
    return false;
}

std::string Session::handleTwoFactor() {
    http::Response findResp = http_.postForm(kDoubleAuthUrl, {{"action", "FIND_APPROACHES"}});
    jsonx::Json info;
    std::string parseError;
    if (!jsonx::Json::parse(findResp.body, info, parseError)) {
        throw std::runtime_error("两步验证响应解析失败：" + parseError);
    }
    std::string result = info.getString("result");
    if (result != "success") throw std::runtime_error("两步验证失败：" + info.getString("msg", result));
    const jsonx::Json *object = info.find("object");
    bool hasTotp = false;
    if (object) {
        const jsonx::Json *flag = object->find("hasTotp");
        if (flag) hasTotp = (flag->type == jsonx::Json::Type::Bool && flag->boolean) ||
                            (flag->type == jsonx::Json::Type::String && util::toLower(flag->string) == "true");
    }
    if (!hasTotp) throw std::runtime_error("该账号未开启 TOTP 两步验证，无法完成登录。");

    http::Response sendResp = http_.postForm(kDoubleAuthUrl, {{"action", "SEND_CODE"}, {"type", "totp"}});
    jsonx::Json sendResult;
    if (!jsonx::Json::parse(sendResp.body, sendResult, parseError) || sendResult.getString("result") != "success") {
        throw std::runtime_error("发送两步验证请求失败：" + sendResult.getString("msg", parseError));
    }

    auto verify = [&](const std::string &value) {
        http::Response resp = http_.postForm(kDoubleAuthUrl, {{"action", "VERITY_TOTP_CODE"}, {"vericode", value}});
        jsonx::Json parsed;
        std::string err;
        if (!jsonx::Json::parse(resp.body, parsed, err)) {
            throw std::runtime_error("两步验证响应解析失败：" + err);
        }
        return parsed;
    };

    jsonx::Json verifyResult;
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::string code = promptTotpCode();
        verifyResult = verify(code);
        if (verifyResult.getString("result") == "success") break;
        std::printf("❌ 验证码校验失败：%s\n", verifyResult.getString("msg", "未知错误").c_str());
    }
    if (verifyResult.getString("result") != "success") {
        throw std::runtime_error("两步验证码校验失败：多次输入仍未通过。");
    }

    // 是否信任本设备：由 --trust-device / --no-trust-device / THU_TRUST_DEVICE 决定，
    // 没配置时在这里问一次，并把答案记在本会话里（同一次运行里再次触发 2FA 不会重复问）。
    if (options_.trustDevice == TrustDevice::Ask) {
        options_.trustDevice = askTrustDevice() ? TrustDevice::Yes : TrustDevice::No;
    }

    if (options_.trustDevice == TrustDevice::No) {
        std::printf("🔓 未信任本设备：下次登录仍会要求输入 6 位 TOTP 验证码。\n");
    } else {
        http::Response trustResp = http_.postForm(
            kSaveFingerUrl, {{"fingerprint", options_.fingerprint}, {"deviceName", "coursesAuto"}, {"radioVal", "是"}});
        jsonx::Json trustResult;
        if (!jsonx::Json::parse(trustResp.body, trustResult, parseError) ||
            trustResult.getString("result") != "success") {
            log("信任本设备失败（可忽略）：" + trustResult.getString("msg", parseError));
        } else {
            std::printf("🔒 已在本机信任该设备：以后的登录通常不再要求 TOTP 验证码"
                        "（可在清华 ID 的设备管理中移除）。\n");
        }
    }

    const jsonx::Json *obj = verifyResult.find("object");
    std::string redirect = obj ? obj->getString("redirectUrl") : "";
    if (redirect.empty()) throw std::runtime_error("两步验证成功，但未获取到跳转地址。");
    std::string target = util::startsWith(redirect, "http") ? redirect : std::string(kIdHostUrl) + redirect;
    return http_.get(target).body;
}

std::string Session::idLogin(const std::string &sm2PublicKey) {
    std::string error;
    std::string encrypted = crypto::sm2Encrypt(sm2PublicKey, std::vector<uint8_t>(options_.password.begin(), options_.password.end()), error);
    if (encrypted.empty()) throw std::runtime_error("密码加密失败：" + error);

    http::Response resp = http_.postForm(
        kIdLoginUrl, {{"i_user", options_.userId},
                      {"i_pass", encrypted},
                      {"fingerPrint", options_.fingerprint},
                      {"fingerGenPrint", ""},
                      {"i_captcha", ""}});
    std::string body = resp.body;
    if (body.find("二次认证") != std::string::npos) {
        log("需要两步验证");
        body = handleTwoFactor();
    }
    if (body.find("登录成功") == std::string::npos) {
        throw std::runtime_error("登录失败：" + errorMessage(body));
    }
    return body;
}

std::string Session::roamId(const std::string &payload) {
    log("正在获取 WebVPN 会话…");
    http::Response page = http_.get(std::string(kIdBaseUrl) + payload);
    std::string key = extractSm2PublicKey(page.body);
    if (key.empty()) throw std::runtime_error("无法获取登录页 SM2 公钥（WebVPN 漫游步骤）。");
    std::string success = idLogin(key);
    std::string callback = absoluteUrl(callbackUrl(success));
    if (callback.empty()) throw std::runtime_error("登录成功但未找到回调链接。");
    std::string target = getWebVpnUrl(callback);
    http::Response resp = http_.get(target);
    if (resp.finalUrl == kLoginUrl) throw std::runtime_error("WebVPN 会话建立失败，请稍后重试。");
    return resp.body;
}

void Session::login() {
    if (loggedIn_) return;
    log("正在登录清华大学信息门户（" + options_.userId + "）…");
    http_.clearCookies();
    http::Response page = http_.get(kWebVpnOauthLoginUrl);
    std::string key = extractSm2PublicKey(page.body);
    if (key.empty()) {
        throw std::runtime_error("无法获取 id.tsinghua.edu.cn 登录页的 SM2 公钥，请确认网络可访问清华门户。");
    }
    std::string success = idLogin(key);
    std::string callback = absoluteUrl(callbackUrl(success));
    if (!callback.empty()) {
        http::Response redirect = http_.get(callback);
        if (redirect.finalUrl == kLoginUrl) throw std::runtime_error("登录回调失败，请稍后重试。");
    }
    roamId(kIdRoamPayload);
    loggedIn_ = true;
    log("登录成功");
}

std::string Session::roamDefault(const std::string &payload) {
    http::Response cookieResp = http_.get(kGetCookieUrl);
    std::string token;
    {
        std::regex re("XSRF-TOKEN=([^;\\s]+)");
        std::smatch m;
        if (std::regex_search(cookieResp.body, m, re)) token = m[1].str();
    }
    if (token.empty()) throw std::runtime_error("无法从 WebVPN 获取 XSRF-TOKEN。");

    std::string url = std::string(kRoamingUrl) + "?yyfwid=" + payload + "&_csrf=" + token + "&machine=p";
    http::Response roamingResp = http_.get(url);
    jsonx::Json json;
    std::string parseError;
    if (!jsonx::Json::parse(roamingResp.body, json, parseError)) {
        throw std::runtime_error("应用漫游响应解析失败：" + parseError);
    }
    const jsonx::Json *object = json.find("object");
    std::string roamingUrl = object ? object->getString("roamingurl") : "";
    if (roamingUrl.empty()) throw std::runtime_error("未获取到应用漫游地址。");
    roamingUrl = util::replaceAll(roamingUrl, "&amp;", "&");
    std::string target = parseRoamingUrl(roamingUrl);
    log("应用漫游 -> " + target);
    http::Response resp = http_.get(target);
    return resp.body;
}

void Session::relogin() {
    log("会话失效，重新登录…");
    loggedIn_ = false;
    login();
}

std::string Session::fetchCalendarRaw() {
    std::string home;
    std::string lastError;
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            if (home.empty()) home = http_.get(kLearnHomeUrl).body;
            std::regex csrfRe("_csrf=([\\w-]+)");
            std::smatch m;
            if (!std::regex_search(home, m, csrfRe)) throw std::runtime_error("未在教务页面中找到 _csrf。");
            http::Response resp = http_.get(std::string(kSemesterListUrl) + m[1].str());
            jsonx::Json json;
            std::string parseError;
            if (!jsonx::Json::parse(resp.body, json, parseError)) {
                throw std::runtime_error("学期列表解析失败：" + parseError);
            }
            if (json.getString("message") != "success") throw std::runtime_error("学期列表接口返回失败。");
            return resp.body;
        } catch (const std::exception &e) {
            lastError = e.what();
            log(std::string("读取学期信息失败：") + lastError);
            try {
                if (attempt == 0) {
                    home = roamDefault(kLearnRoamPayload);
                } else {
                    relogin();
                    home = roamDefault(kLearnRoamPayload);
                }
            } catch (const std::exception &e2) {
                lastError += "；漫游/重登失败：" + std::string(e2.what());
                home.clear();
            }
        }
    }
    throw std::runtime_error("无法读取学期信息：" + lastError);
}

std::string Session::fetchPrimaryRaw(const Semester &semester) {
    const std::string prefix = graduate() ? kJxrlYjsPrefix : kJxrlBksPrefix;
    int firstDay = 0;
    if (!util::parseDateDays(semester.firstDay, firstDay)) throw std::runtime_error("学期第一天格式无效。");
    const int groups = (semester.weekCount + 2) / 3;
    std::string lastError;
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            std::string inner;
            for (int i = 0; i < groups; ++i) {
                int start = firstDay + i * 21;
                int end = firstDay + ((i + 1) * 3 - 1) * 7 + 6;
                std::string url =
                    prefix + util::formatCompact(start) + kJxrlMiddle + util::formatCompact(end) + kJxrlSuffix;
                http::Response resp = http_.get(url);
                std::string body = util::trim(util::stripBom(resp.body));
                if (body.empty()) continue;
                if (body[0] != 'm') throw std::runtime_error("课表接口返回了非预期内容。");
                size_t lb = body.find('[');
                size_t rb = body.rfind(']');
                if (lb == std::string::npos || rb == std::string::npos || rb < lb) continue;
                std::string part = util::trim(body.substr(lb + 1, rb - lb - 1));
                if (part.empty()) continue;
                if (!inner.empty()) inner += ",";
                inner += part;
            }
            return "[" + inner + "]";
        } catch (const std::exception &e) {
            lastError = e.what();
            log(std::string("读取课表失败：") + lastError);
            try {
                if (attempt == 0) {
                    roamDefault(graduate() ? kScheduleRoamPayloadYjs : kScheduleRoamPayloadBks);
                } else {
                    relogin();
                    roamDefault(graduate() ? kScheduleRoamPayloadYjs : kScheduleRoamPayloadBks);
                }
            } catch (const std::exception &e2) {
                lastError += "；漫游/重登失败：" + std::string(e2.what());
            }
        }
    }
    throw std::runtime_error("无法读取课表：" + lastError);
}

std::string Session::fetchSecondaryRaw() {
    std::string lastError;
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            http::Response resp = http_.get(kSecondaryUrl);
            if (resp.body.find("strHTML") == std::string::npos) throw std::runtime_error("二级课表页面中没有课表数据。");
            return resp.body;
        } catch (const std::exception &e) {
            lastError = e.what();
            log(std::string("读取二级课表失败：") + lastError);
            try {
                if (attempt == 0) {
                    roamDefault(kScheduleRoamPayloadBks);
                } else {
                    relogin();
                    roamDefault(kScheduleRoamPayloadBks);
                }
            } catch (const std::exception &e2) {
                lastError += "；漫游/重登失败：" + std::string(e2.what());
            }
        }
    }
    throw std::runtime_error("无法读取二级课表：" + lastError);
}

} // namespace thu
