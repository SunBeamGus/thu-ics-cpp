#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace util {

std::string trim(const std::string &s);
std::string stripBom(const std::string &s);
std::string toLower(std::string s);
bool startsWith(const std::string &s, const std::string &prefix);
bool endsWith(const std::string &s, const std::string &suffix);
std::string replaceAll(std::string s, const std::string &from, const std::string &to);
std::vector<std::string> split(const std::string &s, char sep);

std::string urlEncode(const std::string &s);
std::string hexEncode(const uint8_t *data, size_t len);
std::string hexEncode(const std::vector<uint8_t> &data);
std::vector<uint8_t> hexDecode(const std::string &hex);
std::string base64Encode(const std::vector<uint8_t> &data);

std::string htmlDecode(const std::string &s);
std::string stripTags(const std::string &html);
std::string gbkToUtf8(const std::string &s);

// Proleptic Gregorian calendar <-> days since 1970-01-01.
int daysFromCivil(int y, int m, int d);
void civilFromDays(int z, int &y, int &m, int &d);
std::string formatDate(int days);    // YYYY-MM-DD
std::string formatCompact(int days); // YYYYMMDD
// Extracts the leading YYYY-MM-DD (or YYYY/MM/DD) from a date-time string.
// Returns false if no valid date is found.
bool parseDateDays(const std::string &s, int &days);
// Parses "08:00", "8：00", "08:00:00" into minutes after midnight.
bool parseHhmm(const std::string &s, int &minutes);
std::string nowUtcIcs(); // YYYYMMDDTHHMMSSZ

std::string foldIcsLine(const std::string &line);

bool readFile(const std::string &path, std::string &out);
bool writeFile(const std::string &path, const std::string &content);
bool ensureParentDir(const std::string &path);

} // namespace util
