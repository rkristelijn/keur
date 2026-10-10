/**
 * @file rule/match.cpp
 * @brief Rule/file matching, keur:ignore suppression, and shared helpers.
 */
#include "rule/internal.h"

#include <sstream>

namespace keur {

// --- Action verb: central string<->enum + validation (ADR-058 drift class) ---
Action action_from_string(const std::string& s) {
  if (s == "block") return Action::Block;
  if (s == "warn") return Action::Warn;
  if (s == "advisory") return Action::Advisory;
  if (s == "off") return Action::Off;
  return Action::Unknown;
}

std::string action_to_string(Action a) {
  switch (a) {
    case Action::Block:    return "block";
    case Action::Warn:     return "warn";
    case Action::Advisory: return "advisory";
    case Action::Off:      return "off";
    case Action::Unknown:  return "";
  }
  return "";
}

bool action_is_valid(const std::string& s) {
  return action_from_string(s) != Action::Unknown;
}

namespace detail {

std::string trim(const std::string& s) {
  auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream iss(s);
  std::string p;
  while (iss >> p) out.push_back(p);
  return out;
}

bool ends_with(const std::string& s, const std::string& suf) {
  return suf.size() <= s.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool starts_with(const std::string& s, const std::string& pre) {
  return s.compare(0, pre.size(), pre) == 0;
}

std::string get_ext(const std::string& p) {
  auto dot = p.rfind('.');
  return dot == std::string::npos ? "" : p.substr(dot);
}

std::string basename_of(const std::string& p) {
  auto slash = p.rfind('/');
  return slash == std::string::npos ? p : p.substr(slash + 1);
}

bool looks_binary(std::string_view content, size_t inspect) {
  // git/grep heuristic: a NUL byte in the inspected prefix => binary. Cheap,
  // deterministic, and extension-agnostic (catches .pdf/.png/… without a list).
  size_t n = content.size() < inspect ? content.size() : inspect;
  for (size_t i = 0; i < n; i++)
    if (content[i] == '\0') return true;
  return false;
}

std::string default_action(const Rule& r) {
  if (!r.action.empty()) return r.action;
  if (r.severity == "error" || r.severity == "critical") return "block";
  if (r.severity == "warning") return "warn";
  return "advisory";
}

std::string default_conf(const Rule& r) {
  if (!r.confidence.empty()) return r.confidence;
  switch (action_from_string(default_action(r))) {
    case Action::Block: return "0.9";
    case Action::Warn:  return "0.6";
    default:            return "0.4";
  }
}

std::string finding_msg(const Rule& r, const std::string& pattern_message) {
  if (!pattern_message.empty()) return pattern_message;
  if (!r.message.empty()) return r.message;
  return r.title;
}

void split_lines(const std::string& content, std::vector<re2::StringPiece>& lines) {
  const char* p = content.data();
  const char* end = p + content.size();
  const char* ls = p;
  while (p <= end) {
    if (p == end || *p == '\n') { lines.emplace_back(ls, p - ls); ls = p + 1; }  // keur:ignore KEUR-PORT-007 — std::pair<> for line spans has a constructor — safe
    p++;
  }
}

// keur:ignore [<id|category>] — token-exact so SEC-04 does not suppress SEC-043.
namespace {
bool tokens_match(const std::string& rest, const std::string& id, const std::string& cat) {
  std::istringstream iss(rest);
  std::string tok;
  while (iss >> tok) {
    while (!tok.empty() && tok.back() == ',') tok.pop_back();
    if (!id.empty() && tok == id) return true;
    if (!cat.empty() && tok == cat) return true;
  }
  return false;
}
}  // namespace

bool line_suppresses(std::string_view line, const std::string& id, const std::string& cat) {
  auto pos = line.find("keur:ignore");
  if (pos == std::string_view::npos) return false;
  std::string rest(line.substr(pos + 11));
  auto s = rest.find_first_not_of(" \t:,");
  if (s == std::string::npos) return true;  // bare keur:ignore suppresses all
  return tokens_match(rest.substr(s), id, cat);
}

}  // namespace detail

// Public: does a file's relative path match a rule's target spec?
bool rule_matches_file(const Rule& rule, const std::string& rel) {
  using namespace detail;
  bool has_ext = !rule.target.extensions.empty();
  bool has_name = !rule.target.filenames.empty();
  if (has_ext || has_name) {
    bool match = false;
    if (has_ext) {
      std::string ext = get_ext(rel);
      for (auto& e : rule.target.extensions)
        if (ext == e || ends_with(rel, e)) { match = true; break; }
    }
    if (!match && has_name) {
      std::string b = basename_of(rel);
      for (auto& fn : rule.target.filenames)
        if (b == fn) { match = true; break; }
    }
    if (!match) return false;
  }
  for (auto& ex : rule.target.exclude_paths)
    if (rel.find(ex) != std::string::npos) return false;
  return true;
}

}  // namespace keur
