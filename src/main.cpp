#include "ics.h"
#include "schedule.h"
#include "selftest.h"
#include "sm3.h"
#include "thu.h"
#include "util.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
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
    std::string outDir;
    std::string accountsFile;
    std::string idSalt;
    std::string idMap;
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
        "  --outdir <目录>         输出目录（默认 res），每个学生写 <代号>.ics\n"
        "  --output <路径>         直接指定单个输出文件（多账号时不可用）\n"
        "  --accounts <文件>       批量生成：每行「学号,密码」，# 开头为注释（或 THU_ACCOUNTS）\n"
        "  --id-salt <文本>        学号 → 代号的盐（或 THU_ID_SALT；缺省用本地 .id-salt）\n"
        "  --id-map <文件>         学号-代号对照表（默认 <outdir>/id-map.txt，本地留存）\n"
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

// 输出文件名固定为 <outdir>/<学号派生代号>.ics；--output 只用于显式指定单个文件。
std::filesystem::path exeDirectory() {
    wchar_t buffer[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::filesystem::path();
    return std::filesystem::path(buffer).parent_path();
}

std::string randomHex(size_t bytes) {
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device()};
    std::mt19937_64 generator(seed);
    static const char *kDigits = "0123456789abcdef";
    std::string result;
    result.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
        unsigned value = static_cast<unsigned>(generator() & 0xFFu);
        result.push_back(kDigits[value >> 4]);
        result.push_back(kDigits[value & 0x0F]);
    }
    return result;
}

// 代号用的盐：--id-salt / THU_ID_SALT > 已有的 .id-salt > 自动生成一个并写到当前目录。
// 盐只留在本地，别人拿到文件名的代号也推不出学号。
std::string resolveIdSalt(const std::string &explicitSalt, std::filesystem::path *createdAt) {
    if (!explicitSalt.empty()) return explicitSalt;

    std::vector<std::filesystem::path> candidates{std::filesystem::path(".id-salt")};
    std::filesystem::path exeDir = exeDirectory();
    if (!exeDir.empty()) {
        candidates.push_back(exeDir / ".id-salt");
        candidates.push_back(exeDir / ".." / ".id-salt");
        candidates.push_back(exeDir / ".." / ".." / ".id-salt");
    }
    for (const std::filesystem::path &path : candidates) {
        std::string text;
        if (util::readFile(path.string(), text)) {
            std::string salt = util::trim(text);
            if (!salt.empty()) return salt;
        }
    }

    std::string salt = randomHex(16);
    std::filesystem::path target(".id-salt");
    if (util::writeFile(target.string(), salt + "\r\n") && createdAt != nullptr) {
        *createdAt = std::filesystem::absolute(target);
    }
    return salt;
}

std::string outputPathFor(const Args &args, const std::string &userId, const std::string &idSalt) {
    if (!args.output.empty()) return args.output;
    std::string dir = args.outDir.empty() ? "res" : args.outDir;
    if (userId.empty()) return (std::filesystem::path(dir) / "coursesAuto.ics").string();
    return (std::filesystem::path(dir) / (thu::deriveIdCode(idSalt, userId) + ".ics")).string();
}

// 学号 ↔ 代号 对照表：留在本地方便哪天忘了哪个文件是谁的。
// 含学号，所以只写到 res\ 之类已被 .gitignore 忽略的地方，不随发布走。
// 与已有文件合并：同一学号以本次为准，其它历史记录保留，方便分批跑。
void writeIdMap(const Args &args, const std::vector<std::pair<std::string, std::string>> &entries) {
    std::string dir = args.outDir.empty() ? "res" : args.outDir;
    std::string target = args.idMap.empty() ? (std::filesystem::path(dir) / "id-map.txt").string() : args.idMap;

    std::vector<std::pair<std::string, std::string>> merged; // 代号 -> 学号
    std::string existing;
    if (util::readFile(target, existing)) {
        size_t pos = 0;
        while (pos <= existing.size()) {
            size_t eol = existing.find('\n', pos);
            std::string line = existing.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            pos = (eol == std::string::npos) ? existing.size() + 1 : eol + 1;
            line = util::trim(line);
            if (line.empty() || line[0] == '#') continue;
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::string code = util::trim(line.substr(0, tab));
            std::string id = util::trim(line.substr(tab + 1));
            if (!code.empty() && !id.empty()) merged.push_back({code, id});
        }
    }

    for (const auto &entry : entries) {
        merged.erase(std::remove_if(merged.begin(), merged.end(),
                                    [&](const std::pair<std::string, std::string> &item) {
                                        return item.first == entry.first || item.second == entry.second;
                                    }),
                     merged.end());
        merged.push_back(entry);
    }
    std::sort(merged.begin(), merged.end(),
              [](const std::pair<std::string, std::string> &a, const std::pair<std::string, std::string> &b) {
                  return a.second < b.second;
              });

    // 带 UTF-8 BOM：Windows 记事本 / PowerShell 5.1 都能直接正确显示中文。
    std::string text =
        "\xEF\xBB\xBF"
        "# thu-courses-ics 学号-代号对照表\r\n"
        "# 含学号：仅留在本地，不要提交、不要随站点发布。\r\n"
        "# 代号\t学号\r\n";
    for (const auto &entry : merged) text += entry.first + "\t" + entry.second + "\r\n";

    if (!util::ensureParentDir(target) || !util::writeFile(target, text)) {
        std::fprintf(stderr, "  ! 无法写入对照表：%s\n", target.c_str());
        return;
    }
    std::printf("🗂 对照表已更新：%s（共 %zu 条）\n", target.c_str(), merged.size());
}

// 本次要处理的账号：.env / 命令行里的单账号在前，--accounts 列表在后，按学号去重。
std::vector<thu::Account> resolveAccounts(const Args &args, std::vector<std::string> &problems) {
    std::vector<thu::Account> accounts;
    std::set<std::string> seen;
    if (!args.userId.empty() && !args.password.empty()) {
        accounts.push_back(thu::Account{args.userId, args.password});
        seen.insert(args.userId);
    } else if (!args.userId.empty() || !args.password.empty()) {
        throw std::runtime_error("THU_USER_ID 与 THU_PASSWORD 需要同时提供。");
    }
    if (!args.accountsFile.empty()) {
        std::string text;
        if (!util::readFile(args.accountsFile, text)) {
            throw std::runtime_error("无法读取账号列表：" + args.accountsFile);
        }
        for (const thu::Account &account : thu::parseAccounts(text, &problems)) {
            if (!seen.insert(account.userId).second) {
                problems.push_back("学号 " + account.userId + " 重复（已在 .env 或更早的行里出现），已忽略");
                continue;
            }
            accounts.push_back(account);
        }
    }
    return accounts;
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
    std::string idSalt;
    if (args.output.empty() && !args.userId.empty()) {
        std::filesystem::path created;
        idSalt = resolveIdSalt(args.idSalt, &created);
        if (!created.empty()) {
            std::printf("🔑 已生成本地代号盐：%s（请勿公开或删除，删除后代号会变）\n", created.string().c_str());
        }
    }
    buildAndWrite(courses, semester, args.calName, outputPathFor(args, args.userId, idSalt), args.verbose);
    if (!args.userId.empty() && args.output.empty()) {
        writeIdMap(args, {{thu::deriveIdCode(idSalt, args.userId), args.userId}});
    }
    return 0;
}

int runOnline(const Args &args) {
    std::vector<std::string> problems;
    std::vector<thu::Account> accounts = resolveAccounts(args, problems);
    for (const std::string &problem : problems) std::fprintf(stderr, "  ! %s\n", problem.c_str());
    if (accounts.empty()) {
        throw std::runtime_error("缺少登录凭据：请设置 THU_USER_ID / THU_PASSWORD，或用 --accounts 指定账号列表。");
    }
    if (accounts.size() > 1 && !args.output.empty()) {
        throw std::runtime_error("多账号时不能用 --output 指定单个文件，请改用 --outdir <目录>。");
    }

    const bool batch = accounts.size() > 1;
    size_t failed = 0;
    size_t index = 0;

    // 代号盐只在真正要算文件名时才解析 / 生成。
    std::string idSalt;
    bool saltReady = false;
    auto ensureSalt = [&]() -> const std::string & {
        if (!saltReady) {
            std::filesystem::path created;
            idSalt = resolveIdSalt(args.idSalt, &created);
            saltReady = true;
            if (!created.empty()) {
                std::printf("🔑 已生成本地代号盐：%s（请勿公开或删除，删除后代号会变）\n",
                            created.string().c_str());
            }
        }
        return idSalt;
    };

    std::vector<std::pair<std::string, std::string>> codeMap; // 代号 → 学号

    for (const thu::Account &account : accounts) {
        ++index;
        if (batch) {
            std::printf("\n===== [%zu/%zu] 学号 %s =====\n", index, accounts.size(), account.userId.c_str());
        }
        std::string code;
        try {
            for (char c : account.userId) {
                if (c < '0' || c > '9') throw std::runtime_error("学号应为纯数字。");
            }

            if (args.output.empty()) {
                code = thu::deriveIdCode(ensureSalt(), account.userId);
                std::printf("🆔 学号 %s → 代号 %s（文件名）\n", account.userId.c_str(), code.c_str());
            }

            // 每个学号用各自的设备指纹，避免多账号共用同一台“设备”。
            std::string fingerprint = args.fingerprint.empty() || batch ? defaultFingerprint(account.userId)
                                                                       : args.fingerprint;
            std::string dumpDir = args.dumpDir;
            if (!dumpDir.empty() && batch) {
                dumpDir = (std::filesystem::path(dumpDir) / account.userId).string();
            }

            thu::Options options;
            options.userId = account.userId;
            options.password = account.password;
            options.proxy = args.proxy;
            options.verbose = args.verbose;
            options.trustDevice = args.trustDevice;
            options.fingerprint = fingerprint;

            thu::Session session(options);
            std::printf("🔐 正在登录清华大学信息门户（%s）…\n", account.userId.c_str());
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

            if (!dumpDir.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(dumpDir, ec);
                if (ec) throw std::runtime_error("无法创建导出目录：" + dumpDir);
                if (!util::writeFile((std::filesystem::path(dumpDir) / "calendar.json").string(), calendarRaw) ||
                    !util::writeFile((std::filesystem::path(dumpDir) / "primary.json").string(), primaryRaw) ||
                    (!secondaryHtml.empty() &&
                     !util::writeFile((std::filesystem::path(dumpDir) / "secondary.html").string(), secondaryHtml))) {
                    throw std::runtime_error("写入导出目录失败：" + dumpDir);
                }
                std::printf("💾 原始数据已保存到 %s\n", dumpDir.c_str());
            }

            std::vector<schedule::Course> courses =
                schedule::mergeCourses(std::move(primary.courses), secondary.courses);
            buildAndWrite(courses, semester, args.calName, outputPathFor(args, account.userId, ensureSalt()),
                          args.verbose);
            if (!code.empty()) codeMap.push_back({code, account.userId});
        } catch (const std::exception &e) {
            ++failed;
            std::fprintf(stderr, "❌ 学号 %s 生成失败：%s\n", account.userId.c_str(), e.what());
        }
    }

    if (!codeMap.empty()) writeIdMap(args, codeMap);

    if (failed > 0) {
        std::fprintf(stderr, "❌ %zu/%zu 个账号失败。\n", failed, accounts.size());
        return 1;
    }
    if (batch) std::printf("\n✅ 全部 %zu 个账号已完成。\n", accounts.size());
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
            else if (arg == "--outdir") args.outDir = next("--outdir");
            else if (arg == "--accounts") args.accountsFile = next("--accounts");
            else if (arg == "--id-salt") args.idSalt = next("--id-salt");
            else if (arg == "--id-map") args.idMap = next("--id-map");
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
        if (args.outDir.empty()) args.outDir = envValue(dotenv, "THU_OUTDIR");
        if (args.accountsFile.empty()) args.accountsFile = envValue(dotenv, "THU_ACCOUNTS");
        if (args.idSalt.empty()) args.idSalt = envValue(dotenv, "THU_ID_SALT");
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
        // 输出路径不再有全局默认值：在线模式按 <outdir>/<学号>.ics 逐个计算。

        if (!args.offlineDir.empty()) return runOffline(args);
        return runOnline(args);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "❌ 生成失败：%s\n", e.what());
        return 1;
    }
}
