/**
 * @file learn/sysinfo.h
 * @brief keur learn — machine resources + run duration (one responsibility).
 *
 * NOTE: the implementation is currently macOS-only (mach + sysctl). This is the
 * seam where a cross-platform SysInfo adapter belongs (Linux /proc, Windows
 * GlobalMemoryStatusEx) — see TODO.md. Keeping it in its own module makes that
 * swap local.
 */
#pragma once

#include <string>

namespace keur::learn {

struct Resources {
  double load1 = 0.0;       // 1-min load average
  int ncpu = 0;             // logical cpus
  double ram_total_gb = 0;
  double ram_free_gb = 0;   // free + inactive (macOS: practically available)
  long run_secs = 0;        // how long the current run has been going
};

/** Read CPU load, RAM, and the current run's duration (from the supervisor
 *  pidfile mtime under `home`). */
Resources read_resources(const std::string& home);

/** Format a duration in seconds as "Xh Ym" / "Ym". */
std::string fmt_duration(long secs);

}  // namespace keur::learn
