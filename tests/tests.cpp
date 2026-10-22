#include "selftest.h"

#include <cstdio>
#include <string>

int main() {
    std::string report;
    bool ok = selftest::run(report);
    std::printf("%s\n", report.c_str());
    return ok ? 0 : 1;
}
