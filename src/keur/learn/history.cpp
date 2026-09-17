/**
 * @file learn/history.cpp
 * @brief optimiser history parsing + aggregation.
 */
#include "learn/history.h"

#include <algorithm>
#include <fstream>

namespace keur::learn {

namespace {

// Tiny flat-JSON field extractors: the records have a known flat shape, so a
// full JSON parser would be overkill.
std::string json_str(const std::string& line, const std::string& key) {
  auto pos = line.find("\"" + key + "\"");
  if (pos == std::string::npos) return "";
  pos = line.find(':', pos);
  if (pos == std::string::npos) return "";
  ++pos;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '"')) ++pos;
  std::string out;
  while (pos < line.size() && line[pos] != '"' && line[pos] != ',' && line[pos] != '}') {
    out += line[pos++];
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

double json_num(const std::string& line, const std::string& key) {
  std::string s = json_str(line, key);
  try {
    return s.empty() ? 0.0 : std::stod(s);
  } catch (...) {
    return 0.0;
  }
}

std::string reason_of(const std::string& note) {
  if (note.find("overfit") != std::string::npos) return "overfit";
  if (note.find("holdout") != std::string::npos) return "holdout";
  if (note.find("no-rule") != std::string::npos) return "no-rule";
  if (note.find("gate") != std::string::npos) return "gate";
  return "other";
}

}  // namespace

std::vector<IterRecord> parse_history(const std::string& path) {
  std::vector<IterRecord> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.find("\"iter\"") == std::string::npos) continue;
    IterRecord r;
    r.iter = static_cast<int>(json_num(line, "iter"));
    r.ts = json_str(line, "ts");
    r.decision = json_str(line, "decision");
    r.recall = json_num(line, "recall");
    r.precision = json_num(line, "precision");
    r.note = json_str(line, "note");
    r.source = json_str(line, "source");
    if (r.source.empty()) r.source = "corpus";
    out.push_back(r);
  }
  return out;
}

Agg aggregate(const std::vector<IterRecord>& recs) {
  Agg a;
  for (const auto& r : recs) {
    a.all.push_back(r);
    a.recalls.push_back(r.recall);
    a.precs.push_back(r.precision);
    a.best_recall = std::max(a.best_recall, r.recall);
    a.cur_recall = r.recall;
    if (r.decision == "accept") {
      ++a.n_accept;
      a.accepts.push_back(r);
    } else {
      ++a.n_reject;
      a.reject_reasons[reason_of(r.note)]++;
    }
  }
  return a;
}

}  // namespace keur::learn
