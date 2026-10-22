#pragma once

#include <string>

namespace selftest {
// Runs offline unit tests. Appends a human-readable report to `report`.
bool run(std::string &report);
} // namespace selftest
