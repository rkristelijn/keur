/**
 * @file learn/sysinfo.cpp
 * @brief Compose host resources (from the SysInfo adapter) with keur's own
 *        run duration. No platform system calls live here — they are behind the
 *        adapter — so this module builds identically on every platform.
 */
#include "learn/sysinfo.h"

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace keur::learn {

Resources read_resources(const std::string& home, const adapters::SysInfo& sys) {
  Resources r;

  // Platform resources come from the adapter (macOS/Linux/Windows behind its
  // own #if); tests can inject a fake by passing a SysInfo with `read` set.
  adapters::Resources hw = sys.read();
  r.load1 = hw.load1;
  r.ncpu = hw.ncpu;
  r.ram_total_gb = hw.ram_total_gb;
  r.ram_free_gb = hw.ram_free_gb;

  // Run duration from the supervisor pidfile mtime (set when the run started).
  // Portable: std::filesystem works on all platforms.
  namespace fs = std::filesystem;
  std::error_code ec;
  auto pidf = fs::path(home) / "eval/.run/supervisor.pid";
  if (fs::exists(pidf, ec)) {
    auto ft = fs::last_write_time(pidf, ec);
    if (!ec) {
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
      auto now = std::chrono::system_clock::now();
      r.run_secs = std::chrono::duration_cast<std::chrono::seconds>(now - sctp).count();
      if (r.run_secs < 0) r.run_secs = 0;
    }
  }
  return r;
}

std::string fmt_duration(long secs) {
  long h = secs / 3600, m = (secs % 3600) / 60;
  char buf[32];
  if (h > 0) std::snprintf(buf, sizeof(buf), "%ldh %ldm", h, m);
  else       std::snprintf(buf, sizeof(buf), "%ldm", m);
  return buf;
}

}  // namespace keur::learn
