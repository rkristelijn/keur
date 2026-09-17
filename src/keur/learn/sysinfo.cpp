/**
 * @file learn/sysinfo.cpp
 * @brief machine resources + run duration (macOS impl; cross-platform TODO).
 */
#include "learn/sysinfo.h"

#include <sys/sysctl.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace keur::learn {

Resources read_resources(const std::string& home) {
  Resources r;
  size_t sz = 0;
  if (sysctlbyname("vm.loadavg", nullptr, nullptr, nullptr, 0) == 0) {
    struct loadavg lv {};
    sz = sizeof(lv);
    if (sysctlbyname("vm.loadavg", &lv, &sz, nullptr, 0) == 0)
      r.load1 = (double)lv.ldavg[0] / (double)lv.fscale;
  }
  { int n = 0; sz = sizeof(n);
    if (sysctlbyname("hw.logicalcpu", &n, &sz, nullptr, 0) == 0) r.ncpu = n; }
  { uint64_t m = 0; sz = sizeof(m);
    if (sysctlbyname("hw.memsize", &m, &sz, nullptr, 0) == 0)
      r.ram_total_gb = (double)m / 1e9; }
#if defined(__APPLE__)
  { vm_size_t page = 0; host_page_size(mach_host_self(), &page);
    vm_statistics64_data_t vm {};
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &cnt) == KERN_SUCCESS) {
      r.ram_free_gb = (double)((vm.free_count + vm.inactive_count) * page) / 1e9;
    } }
#endif
  // run duration from the supervisor pidfile mtime (set when the run started)
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
