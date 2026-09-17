/**
 * @file cmd/learn.h
 * @brief keur learn — spin the optimise loop in the background and show a
 *        responsive TUI monitor in the foreground.
 *
 *   keur learn            start/attach the loop, show the live monitor
 *   keur learn --stop     stop the background loop
 *   keur learn --status   one-shot status, no watch loop
 *
 * The heavy lifting (Ollama rule tuning) stays in eval/optimise.sh, supervised
 * by eval/supervise.sh. This command orchestrates: it starts the supervisor if
 * nothing is running, then renders the monitor. Ctrl+C leaves the monitor; the
 * background loop keeps running (use --stop to end it).
 */
#pragma once

#include <string>
#include <vector>

namespace keur::cmd {

struct LearnOptions {
  bool stop = false;      // --stop: stop the background loop and exit
  bool status = false;    // --status: print one-shot status, no watch
  bool no_spin = false;   // --no-spin: attach monitor only, do not start a loop
  int refresh_secs = 2;   // monitor refresh interval
  std::string source = "corpus";  // KEUR_EVAL_SOURCE for a freshly spun loop
  std::string model;      // --model: Ollama model for a freshly spun loop (empty = optimise.sh default)
};

LearnOptions learn_parse(const std::vector<std::string>& args);
int learn(const LearnOptions& opts);

// --- data model (parsed from eval/history/optimiser.jsonl) ------------------

struct IterRecord {
  int iter = 0;
  std::string ts;
  std::string decision;   // "accept" | "reject"
  double recall = 0.0;
  double precision = 0.0;
  std::string note;
  std::string source;     // "corpus" | "keureval" | "" (unknown/legacy)
};

/// Parse the optimiser history JSONL. Tolerant of missing fields; ignores
/// lines that fail to parse. Newest records are last.
std::vector<IterRecord> parse_history(const std::string& path);

}  // namespace keur::cmd
