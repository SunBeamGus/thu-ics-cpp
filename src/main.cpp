#include "ics.h"
#include "schedule.h"
#include "selftest.h"
#include "sm3.h"
#include "thu.h"
#include "util.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const char *kVersion = "1.0.0";

std::wstring utf8ToWide(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string wideToUtf8(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

struct Args {
    std::string userId;
    std::string password;
    std::string semesterIndex;
    std::string output;
    std::string calName;
    std::string proxy;
    bool proxyDirect = false;
    std::string fingerprint;
    std::string dumpDir;
    std::string offlineDir;
    thu::TrustDevice trustDevice = thu::TrustDevice::Ask;
    bool trustDeviceSet = false;
    bool selfTest = false;
    bool verbose = false;
    bool help = false;
};

void printHelp() {
    std::printf(
        "thu-courses-ics %s\n"
        "自动读取清华大学课表并生成 iCalendar (.ics) 文件。\n"
        "\n"
        "用法：thu-courses-ics [选项]\n"
        "\n"
        "  --user-id <学号>        信息门户账号（或环境变量 THU_USER_ID）\n"
        "  --password <密码>       信息门户密码（或环境变量 THU_PASSWORD）\n"
        "  --semester <N>          抓取下学期课表：nextSemesterList 下标（0 起；默认当前学期）\n"
        "  --output <路径>         输出文件（默认 res/coursesAuto.ics）\n"
        "  --calname <名称>        日历名称（默认「清华大学课表（学期名）」）\n"
        "  --proxy <host:port>     使用 HTTP 代理；--proxy system 系统代理；--proxy direct 强制直连\n"
        "  --fingerprint <hex>     设备指纹（默认根据账号与本机名自动生成）\n"
        "  --trust-device          两步验证时在清华 ID 内信任本设备（以后可跳过验证码）\n"
        "  --no-trust-device       不信任本设备（每次都要求输入 TOTP 验证码）\n"
        "                          两者都不写时，触发两步验证后会询问一次（或读 THU_TRUST_DEVICE）\n"
        "  --dump-dir <目录>       把原始接口数据保存到目录，便于离线重新生成\n"
        "  --offline <目录>        不登录，直接用 --dump-dir 保存的数据生成 ICS\n"
        "  --self-test             运行内置测试（不需要网络与账号）\n"
        "  --verbose               输出调试信息\n"
        "  -h, --help              显示本帮助\n"
        "\n"
        "凭据也可以写在当前目录（或程序目录）的 .env 文件中，见 .env.example。\n",
        kVersion);
}

std::map<std::string, std::string> loadDotEnvFiles() {
    std::map<std::string, std::string> values;
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back(".env");
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        candidates.push_back(exeDir / ".env");
        candidates.push_back(exeDir / ".." / ".env");
        candidates.push_back(exeDir / ".." / ".." / ".env");
    }
    for (const auto &path : candidates) {
        std::ifstream file(path);
        if (!file) continue;
        std::string line;
        while (std::getline(file, line)) {
            std::string trimmed = util::trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;
            size_t eq = trimmed.find('=');
            if (eq == std::string::npos) continue;
            std::string key = util::trim(util::stripBom(trimmed.substr(0, eq)));
            std::string value = util::trim(trimmed.substr(eq + 1));
            if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
                value = value.substr(1, value.size() - 2);
            }
            if (!key.empty()) values.emplace(key, value);
        }
    }
    return values;
}

std::string envValue(const std::map<std::string, std::string> &dotenv, const std::string &name) {
    std::wstring wname = utf8ToWide(name);
    DWORD len = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (len > 0) {
        std::wstring buffer(len, L'\0');
        DWORD written = GetEnvironmentVariableW(wname.c_str(), &buffer[0], len);
        if (written > 0) {
            buffer.resize(written);
            return wideToUtf8(buffer);
        }
    }
    auto it = dotenv.find(name);
    if (it != dotenv.end()) return it->second;
    return "";
}

std::string defaultFingerprint(const std::string &userId) {
    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer, &size);
    std::string seed = userId + "|" + wideToUtf8(std::wstring(computer, size)) + "|thu-courses-ics";
    std::string digest = crypto::sm3Hex(seed);
    return digest.substr(0, 32);
}

bool readText(const std::filesystem::path &path, std::string &out, bool required, const std::string &label) {
    if (!util::readFile(path.string(), out)) {
        if (required) throw std::runtime_error("无法读取 " + label + "：" + path.string());
        return false;
    }
    return true;
}

thu::Semester chooseSemester(const thu::CalendarData &calendar, const std::string &indexText, bool &isNext) {
    isNext = false;
    if (indexText.empty() || indexText == "current" || indexText == "-1") return calendar.current;
    char *end = nullptr;
    long parsed = std::strtol(indexText.c_str(), &end, 10);
    if (end == indexText.c_str() || (end != nullptr && *end != '\0')) {
        throw std::runtime_error("--semester 需要一个整数下标。");
    }
    int index = static_cast<int>(parsed);
    if (index < 0 || static_cast<size_t>(index) >= calendar.next.size()) {
        throw std::runtime_error("--semester 下标超出范围（共 " + std::to_string(calendar.next.size()) +
                                 " 个下学期可选）。");
    }
    isNext = true;
    return calendar.next[static_cast<size_t>(index)];
}

void printWarnings(const std::vector<std::string> &warnings) {
    for (const std::string &warning : warnings) std::fprintf(stderr, "  ! %s\n", warning.c_str());
}

void buildAndWrite(const std::vector<schedule::Course> &courses, const thu::Semester &semester,
                   const std::string &calName, const std::string &outputPath, bool verbose) {
    std::vector<ics::Event> events = ics::buildEvents(courses, semester);
    std::string name = calName.empty() ? ("清华大学课表（" + semester.semesterName + "）") : calName;
    std::string text = ics::generate(events, name, "清华大学课表（由 thu-courses-ics 自动生成）");
    if (!util::ensureParentDir(outputPath) || !util::writeFile(outputPath, text)) {
        throw std::runtime_error("写入文件失败：" + outputPath);
    }
    std::printf("✅ 已生成 %s\n", outputPath.c_str());
    std::printf("   学期：%s（第 1 周周一 = %s，共 %d 周）\n", semester.semesterName.c_str(), semester.firstDay.c_str(),
                semester.weekCount);
    std::printf("   课程：%zu 门，事件：%zu 个\n", courses.size(), events.size());
    if (verbose) {
        for (const auto &course : courses) {
            std::printf("   - %s @ %s (%zu 个时间片, %zu 场考试)\n", course.name.c_str(), course.location.c_str(),
                        course.slices.size(), course.exams.size());
        }
    }
}

int runOffline(const Args &args) {
    std::filesystem::path dir(args.offlineDir);
    std::string calendarRaw;
    readText(dir / "calendar.json", calendarRaw, true, "calendar.json");
    thu::CalendarData calendar = schedule::parseCalendarJson(calendarRaw);
    bool isNext = false;
    thu::Semester semester = chooseSemester(calendar, args.semesterIndex, isNext);

    std::string primaryRaw;
    readText(dir / "primary.json", primaryRaw, true, "primary.json");
    schedule::ParseResult primary = schedule::parsePrimaryJson(primaryRaw, semester);
    printWarnings(primary.warnings);

    schedule::ParseResult secondary;
    std::string secondaryHtml;
    if (readText(dir / "secondary.html", secondaryHtml, false, "secondary.html") && !secondaryHtml.empty()) {
        secondary = schedule::parseSecondaryHtml(secondaryHtml);
        printWarnings(secondary.warnings);
    }

    std::vector<schedule::Course> courses = schedule::mergeCourses(std::move(primary.courses), secondary.courses);
    buildAndWrite(courses, semester, args.calName, args.output, args.verbose);
    return 0;
}

int runOnline(const Args &args) {
    if (args.userId.empty() || args.password.empty()) {
        throw std::runtime_error("缺少登录凭据：请设置 THU_USER_ID / THU_PASSWORD，或写入 .env 文件。");
    }
    for (char c : args.userId) {
        if (c < '0' || c > '9') throw std::runtime_error("学号应为纯数字。");
    }

    thu::Options options;
    options.userId = args.userId;
    options.password = args.password;
    options.proxy = args.proxy;
    options.verbose = args.verbose;
    options.trustDevice = args.trustDevice;
    options.fingerprint = args.fingerprint.empty() ? defaultFingerprint(args.userId) : args.fingerprint;

    thu::Session session(options);
    std::printf("🔐 正在登录清华大学信息门户…\n");
    session.login();

    std::printf("📚 正在读取学期信息…\n");
    std::string calendarRaw = session.fetchCalendarRaw();
    thu::CalendarData calendar = schedule::parseCalendarJson(calendarRaw);
    bool isNext = false;
    thu::Semester semester = chooseSemester(calendar, args.semesterIndex, isNext);

    std::printf("📅 正在读取课表：%s…\n", semester.semesterName.c_str());
    std::string primaryRaw = session.fetchPrimaryRaw(semester);
    schedule::ParseResult primary = schedule::parsePrimaryJson(primaryRaw, semester);
    printWarnings(primary.warnings);

    std::string secondaryHtml;
    schedule::ParseResult secondary;
    if (!session.graduate()) {
        try {
            secondaryHtml = session.fetchSecondaryRaw();
            secondary = schedule::parseSecondaryHtml(secondaryHtml);
            printWarnings(secondary.warnings);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "  ! 二级课表读取失败，已跳过：%s\n", e.what());
        }
    }

    if (!args.dumpDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(args.dumpDir, ec);
        if (ec) throw std::runtime_error("无法创建导出目录：" + args.dumpDir);
        if (!util::writeFile((std::filesystem::path(args.dumpDir) / "calendar.json").string(), calendarRaw) ||
            !util::writeFile((std::filesystem::path(args.dumpDir) / "primary.json").string(), primaryRaw) ||
            (!secondaryHtml.empty() &&
             !util::writeFile((std::filesystem::path(args.dumpDir) / "secondary.html").string(), secondaryHtml))) {
            throw std::runtime_error("写入导出目录失败：" + args.dumpDir);
        }
        std::printf("💾 原始数据已保存到 %s\n", args.dumpDir.c_str());
    }

    std::vector<schedule::Course> courses = schedule::mergeCourses(std::move(primary.courses), secondary.courses);
    buildAndWrite(courses, semester, args.calName, args.output, args.verbose);
    return 0;
}

} // namespace

std::vector<std::string> commandLineArgs() {
    std::vector<std::string> result;
    int count = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv == nullptr) return result;
    for (int i = 0; i < count; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 1) {
            result.emplace_back();
            continue;
        }
        std::string value(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &value[0], bytes, nullptr, nullptr);
        value.resize(static_cast<size_t>(bytes - 1));
        result.push_back(std::move(value));
    }
    LocalFree(argv);
    return result;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::vector<std::string> arguments = commandLineArgs();
    Args args;
    for (size_t i = 1; i < arguments.size(); ++i) {
        std::string arg = arguments[i];
        auto next = [&](const char *name) -> std::string {
            if (i + 1 >= arguments.size()) throw std::runtime_error(std::string("选项缺少参数：") + name);
            return arguments[++i];
        };
        try {
            if (arg == "--user-id") args.userId = next("--user-id");
            else if (arg == "--password") args.password = next("--password");
            else if (arg == "--totp") {
                next("--totp");
                std::fprintf(stderr, "提示：--totp 已弃用；登录时会提示手动输入 6 位验证码。\n");
            }
            else if (arg == "--semester") args.semesterIndex = next("--semester");
            else if (arg == "--output") args.output = next("--output");
            else if (arg == "--calname") args.calName = next("--calname");
            else if (arg == "--proxy") {
                args.proxy = next("--proxy");
                if (args.proxy == "direct") {
                    args.proxy.clear();
                    args.proxyDirect = true;
                }
            }
            else if (arg == "--fingerprint") args.fingerprint = next("--fingerprint");
            else if (arg == "--trust-device") {
                args.trustDevice = thu::TrustDevice::Yes;
                args.trustDeviceSet = true;
            }
            else if (arg == "--no-trust-device") {
                args.trustDevice = thu::TrustDevice::No;
                args.trustDeviceSet = true;
            }
            else if (arg == "--dump-dir") args.dumpDir = next("--dump-dir");
            else if (arg == "--offline") args.offlineDir = next("--offline");
            else if (arg == "--self-test") args.selfTest = true;
            else if (arg == "--verbose") args.verbose = true;
            else if (arg == "-h" || arg == "--help") args.help = true;
            else throw std::runtime_error("未知选项：" + arg);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "❌ %s\n", e.what());
            return 2;
        }
    }

    if (args.help) {
        printHelp();
        return 0;
    }

    if (args.selfTest) {
        std::string report;
        return selftest::run(report) ? 0 : 1;
    }

    try {
        auto dotenv = loadDotEnvFiles();
        if (args.userId.empty()) args.userId = envValue(dotenv, "THU_USER_ID");
        if (args.password.empty()) args.password = envValue(dotenv, "THU_PASSWORD");
        if (args.semesterIndex.empty()) args.semesterIndex = envValue(dotenv, "THU_SEMESTER_INDEX");
        if (args.output.empty()) args.output = envValue(dotenv, "THU_OUTPUT");
        if (args.calName.empty()) args.calName = envValue(dotenv, "THU_CALNAME");
        if (args.proxy.empty() && !args.proxyDirect) {
            args.proxy = envValue(dotenv, "HTTPS_PROXY");
            if (args.proxy.empty()) args.proxy = envValue(dotenv, "https_proxy");
            if (args.proxy == "direct") args.proxy.clear();
        }
        if (args.fingerprint.empty()) args.fingerprint = envValue(dotenv, "THU_FINGERPRINT");
        if (!args.trustDeviceSet) {
            std::string trustText = envValue(dotenv, "THU_TRUST_DEVICE");
            if (!trustText.empty()) {
                args.trustDevice = thu::parseTrustDevice(trustText);
                args.trustDeviceSet = true;
            }
        }
        if (args.dumpDir.empty()) args.dumpDir = envValue(dotenv, "THU_DUMP_DIR");
        if (args.offlineDir.empty()) args.offlineDir = envValue(dotenv, "THU_OFFLINE");
        if (args.output.empty()) args.output = "res/coursesAuto.ics";

        if (!args.offlineDir.empty()) return runOffline(args);
        return runOnline(args);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "❌ 生成失败：%s\n", e.what());
        return 1;
    }
}
