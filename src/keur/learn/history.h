/**
 * @file learn/history.h
 * @brief keur learn — optimiser history model + aggregation (data only).
 *
 * One responsibility: turn eval/history/optimiser.jsonl into records and
 * aggregate them per source. No I/O beyond reading the file, no rendering.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace keur::learn {

/** One optimiser iteration record, parsed from optimiser.jsonl. */
struct IterRecord {
  int iter = 0;
  std::string ts;
  std::string decision;   // "accept" | "reject"
  double recall = 0.0;
  double precision = 0.0;
  std::string note;
  std::string source;     // "corpus" | "keureval" | "" (legacy -> corpus)
};

/** Parse the optimiser history JSONL. Tolerant of missing fields; skips lines
 *  that do not parse. Newest records last. */
std::vector<IterRecord> parse_history(const std::string& path);

/** Aggregated view of one source's records, ready to render. */
struct Agg {
  std::vector<double> recalls;
  std::vector<double> precs;
  std::vector<IterRecord> accepts;
  std::vector<IterRecord> all;
  std::map<std::string, int> reject_reasons;
  int n_accept = 0, n_reject = 0;
  double best_recall = 0.0, cur_recall = 0.0;
};

/** Aggregate a record set (counts, curves, reject-reason tally). */
Agg aggregate(const std::vector<IterRecord>& recs);

}  // namespace keur::learn
