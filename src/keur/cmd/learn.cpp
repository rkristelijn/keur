/**
 * @file cmd/learn.cpp
 * @brief keur learn — orchestration only: parse options, spin the background
 *        loop (via supervise.sh through the Proc adapter), run the monitor.
 *        Data/rendering/resources live in learn/{history,monitor,sysinfo}.
 */
#include "cmd/learn.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "adapters/proc.h"
#include "common/platform.h"
#include "learn/monitor.h"

namespace keur::cmd {

namespace {

std::string keur_home() {
  if (const char* h = std::getenv("KEUR_HOME")) return h;
  return ".";
}

std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop = true; }

}  // namespace

LearnOptions learn_parse(const std::vector<std::string>& args) {
  LearnOptions o;
  for (size_t i = 0; i < args.size(); ++i) {
    const auto& a = args[i];
    if (a == "--stop") o.stop = true;
    else if (a == "--status") o.status = true;
    else if (a == "--no-spin") o.no_spin = true;
    else if (a == "--source" && i + 1 < args.size()) o.source = args[++i];
    else if (a == "--model" && i + 1 < args.size()) o.model = args[++i];
    else if (a == "--refresh" && i + 1 < args.size()) o.refresh_secs = std::atoi(args[++i].c_str());
  }
  return o;
}

int learn(const LearnOptions& opts) {
  const std::string home = keur_home();
  // All external side effects go through the Proc adapter (ADR-003 / KEUR-ARCH-001):
  // no raw process spawning in a cmd/ orchestrator.
  adapters::Proc proc;

  if (opts.stop) {
    // Shell-free: executable + literal arg in argv, no /bin/sh. @see KEUR-SEC-006.
    return proc.run_argv({home + "/eval/supervise.sh", "stop"}, {});
  }

  if (opts.status) {
    keur::learn::draw_frame(home);
    return 0;
  }

  // spin the background loop if nothing is running and not told to skip
  keur::learn::Supervisor sup = keur::learn::read_supervisor(home);
  if (!sup.running && !opts.no_spin) {
    // Spawn the supervisor shell-free: the executable + its literal args go in
    // argv (no /bin/sh, so `home` can never break out — CWE-78), and what used
    // to be a `VAR=val ... cmd` shell prefix becomes explicit environment
    // overrides. supervise.sh reads these from the environment as before.
    // @see KEUR-SEC-006, ADR-003, ADR-005.
    std::vector<std::pair<std::string, std::string>> env;
    // MODEL is honoured by optimise.sh (env override); pass it only when set so
    // an empty --model falls back to optimise.sh's own default selection.
    if (!opts.model.empty()) env.emplace_back("MODEL", opts.model);
    env.emplace_back("KEUR_PHASES", "loop");
    // Loop command: normally optimise.sh on one source; with --source alternate
    // use alternate.sh, which flips corpus<->keureval each phase.
    if (opts.source == "alternate" || opts.source == "both") {
      env.emplace_back("KEUR_LOOP_CMD", home + "/eval/alternate.sh");
    } else {
      env.emplace_back("KEUR_EVAL_SOURCE", opts.source);
      env.emplace_back("KEUR_LOOP_CMD", home + "/eval/optimise.sh iterate 500");
    }
    std::vector<std::string> argv = {home + "/eval/supervise.sh", "start"};
    std::cerr << "keur learn: spinning background loop (source=" << opts.source;
    if (!opts.model.empty()) std::cerr << ", model=" << opts.model;
    std::cerr << ", runs until 'keur learn --stop')...\n";
    proc.run_argv(argv, env);
  }

  // foreground monitor loop
  std::signal(SIGINT, on_sigint);
  while (!g_stop) {
    keur::learn::draw_frame(home);
    for (int i = 0; i < opts.refresh_secs * 10 && !g_stop; ++i) {
      keur::platform::sleep_ms(100);  // 100ms slices so Ctrl+C stays responsive
    }
  }
  std::cout << "\nmonitor closed. loop still running in background "
               "(keur learn --stop to end it).\n";
  return 0;
}

}  // namespace keur::cmd
