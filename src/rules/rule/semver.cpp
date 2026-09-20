/**
 * @file rule/semver.cpp
 * @brief Tiny version comparator (ADR-009). No solver, no graph — direct pins.
 * @implements ADR-009.requires
 */
#include "rule/semver.h"

#include <cctype>
#include <sstream>
#include <vector>

namespace keur {

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Parse a dotted version into numeric components, stopping at the first
// non-numeric/non-dot char (so "4.18.0-alpha" -> {4,18,0}).
std::vector<int> parts_of(const std::string& v) {
  std::vector<int> parts;
  std::string cur;
  for (char c : v) {
    if (std::isdigit((unsigned char)c)) {
      cur += c;
    } else if (c == '.') {
      parts.push_back(cur.empty() ? 0 : std::stoi(cur));
      cur.clear();
    } else {
      break;  // stop at pre-release/build suffix
    }
  }
  if (!cur.empty()) parts.push_back(std::stoi(cur));
  return parts;
}

}  // namespace

int semver_compare(const std::string& a, const std::string& b) {
  std::vector<int> pa = parts_of(a), pb = parts_of(b);
  size_t n = pa.size() > pb.size() ? pa.size() : pb.size();
  for (size_t i = 0; i < n; i++) {
    int x = i < pa.size() ? pa[i] : 0;
    int y = i < pb.size() ? pb[i] : 0;
    if (x < y) return -1;
    if (x > y) return 1;
  }
  return 0;
}

bool semver_satisfies(const std::string& version_in, const std::string& constraint_in) {
  std::string version = trim(version_in);
  std::string constraint = trim(constraint_in);
  if (version.empty() || constraint.empty()) return false;
  if (parts_of(version).empty()) return false;  // unparseable pin => not satisfied

  // Range:  "A - B"  (inclusive)
  size_t dash = constraint.find(" - ");
  if (dash != std::string::npos) {
    std::string lo = trim(constraint.substr(0, dash));
    std::string hi = trim(constraint.substr(dash + 3));
    return semver_compare(version, lo) >= 0 && semver_compare(version, hi) <= 0;
  }

  // Operator prefix.
  std::string op = "=";
  std::string rhs = constraint;
  if (constraint.rfind("<=", 0) == 0)      { op = "<="; rhs = constraint.substr(2); }
  else if (constraint.rfind(">=", 0) == 0) { op = ">="; rhs = constraint.substr(2); }
  else if (constraint.rfind("<", 0) == 0)  { op = "<";  rhs = constraint.substr(1); }
  else if (constraint.rfind(">", 0) == 0)  { op = ">";  rhs = constraint.substr(1); }
  else if (constraint.rfind("==", 0) == 0) { op = "=";  rhs = constraint.substr(2); }
  else if (constraint.rfind("=", 0) == 0)  { op = "=";  rhs = constraint.substr(1); }
  rhs = trim(rhs);

  int cmp = semver_compare(version, rhs);
  if (op == "<")  return cmp < 0;
  if (op == "<=") return cmp <= 0;
  if (op == ">")  return cmp > 0;
  if (op == ">=") return cmp >= 0;
  return cmp == 0;  // "=" / bare
}

}  // namespace keur
