/**
 * @file cmd/train.h
 * @brief keur train — spin the optimise loop in the background and show a
 *        responsive TUI monitor in the foreground. ORCHESTRATION ONLY.
 *
 *   keur train                       start/attach the loop, show the monitor
 *   keur train --stop                stop the background loop
 *   keur train --status              one-shot status, no watch loop
 *   keur train --source S            corpus | keureval | alternate
 *   keur train --model M             Ollama model for a freshly spun loop
 *
 * `train` TRAINS keur's own rule set (grows rules/learned/ against ground
 * truth) — it is keur that gets trained, not the user (ADR-036). The prior
 * verb for this was `keur learn`, kept as a deprecated alias (see cmd/learn.h).
 *
 * The data model + parsing live in learn/history.h, resources in
 * learn/sysinfo.h, rendering in learn/monitor.h. This file is only the
 * command: parse options + orchestrate (spin loop via supervise.sh, run the
 * monitor). The heavy lifting (Ollama rule tuning) stays in eval/optimise.sh.
 */
#pragma once

#include <string>
#include <vector>

namespace keur::cmd {

struct TrainOptions {
  bool stop = false;      // --stop: stop the background loop and exit
  bool status = false;    // --status: print one-shot status, no watch
  bool no_spin = false;   // --no-spin: attach monitor only, do not start a loop
  int refresh_secs = 2;   // monitor refresh interval
  std::string source = "corpus";  // corpus | keureval | alternate
  std::string model;      // --model: Ollama model (empty = optimise.sh default)
  std::string explain_id; // --explain <RULE-ID>: back-fill that rule's explainer, then exit
};

TrainOptions train_parse(const std::vector<std::string>& args);
int train(const TrainOptions& opts);

}  // namespace keur::cmd
