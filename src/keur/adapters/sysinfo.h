/**
 * @file adapters/sysinfo.h
 * @brief System-info adapter — the single touch point for reading host
 *        resources (CPU load, logical CPU count, RAM total/free).
 *
 * `keur learn`'s resource readout needs numbers that only the OS can give, and
 * every OS gives them differently (macOS mach/sysctl, Linux /proc, Windows
 * Win32). Rather than scatter per-OS guards through cmd/learn, the platform I/O
 * is centralised in the platform layer (common/) and this adapter delegates:
 *   - one interface — { load1, ncpu, ram_total_gb, ram_free_gb },
 *   - the real per-platform reads live in keur::platform::read_resources(),
 *   - a stable seam for tests (inject a fake SysInfo — no real syscalls),
 *   - no raw platform system calls inside cmd/ (a KEUR-ARCH-001 smell).
 *
 * cmd/learn depends on this adapter, not on the platform. Same injectable-seam
 * benefit as the Git/Fs/Proc adapters.
 *
 * @see docs/adr/ADR-003-provider-architecture.md
 * @implements ADR-003
 */
#pragma once
#include <functional>

namespace keur::adapters {

/** A snapshot of host resources. Any field the platform cannot supply is 0. */
struct Resources {
    double load1        = 0.0;  // 1-min load average (0 where the OS has none, e.g. Windows)
    int    ncpu         = 0;    // logical CPUs
    double ram_total_gb = 0.0;  // total physical RAM
    double ram_free_gb  = 0.0;  // available RAM (free + reclaimable)
};

/**
 * System-info operations. `read()` is implemented once per platform in
 * sysinfo.cpp; tests substitute a fake by assigning the function member.
 */
struct SysInfo {
    /** Read a fresh snapshot of host resources. */
    std::function<Resources()> read = default_read;

    // Real implementation (platform-selected in sysinfo.cpp).
    static Resources default_read();
};

}  // namespace keur::adapters
