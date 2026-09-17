/**
 * @file cmd/learn.h
 * @brief keur learn — spin the optimise loop in the background and show a
 *        responsive TUI monitor in the foreground. ORCHESTRATION ONLY.
 *
 *   keur learn                       start/attach the loop, show the monitor
 *   keur learn --stop                stop the background loop
 *   keur learn --status              one-shot status, no watch loop
 *   keur learn --source S            corpus | keureval | alternate
 *   keur learn --model M             Ollama model for a freshly spun loop
 *
 * The data model + parsing live in cmd/learn/history.h, resources in
 * cmd/learn/sysinfo.h, rendering in cmd/learn/monitor.h. This file is only the
 * command: parse options + orchestrate (spin loop via supervise.sh, run the
 * monitor). The heavy lifting (Ollama rule tuning) stays in eval/optimise.sh.
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
  std::string source = "corpus";  // corpus | keureval | alternate
  std::string model;      // --model: Ollama model (empty = optimise.sh default)
};

LearnOptions learn_parse(const std::vector<std::string>& args);
int learn(const LearnOptions& opts);

}  // namespace keur::cmd
