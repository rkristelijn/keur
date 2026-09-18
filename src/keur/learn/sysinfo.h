/**
 * @file learn/sysinfo.h
 * @brief keur learn — machine resources + run duration (one responsibility).
 *
 * The platform-specific resource reads (CPU load, cpu count, RAM) live in the
 * SysInfo adapter (adapters/sysinfo.h) so nothing here depends on the OS. This
 * module composes that adapter with keur's own portable run-duration (derived
 * from the supervisor pidfile mtime) into one struct for the monitor to render,
 * and injects the adapter so tests can feed fake resource numbers.
 *
 * @see adapters/sysinfo.h, docs/adr/ADR-003-provider-architecture.md
 */
#pragma once

#include <string>

#include "adapters/sysinfo.h"

namespace keur::learn {

/** Resources snapshot for the monitor: host numbers + this run's duration. */
struct Resources {
  double load1 = 0.0;       // 1-min load average
  int ncpu = 0;             // logical cpus
  double ram_total_gb = 0;
  double ram_free_gb = 0;   // available RAM
  long run_secs = 0;        // how long the current run has been going
};

/** Read host resources (via the SysInfo adapter) plus the current run's
 *  duration (from the supervisor pidfile mtime under `home`). The adapter is
 *  injectable so tests can supply deterministic numbers. */
Resources read_resources(const std::string& home,
                         const adapters::SysInfo& sys = adapters::SysInfo{});

/** Format a duration in seconds as "Xh Ym" / "Ym". */
std::string fmt_duration(long secs);

}  // namespace keur::learn
