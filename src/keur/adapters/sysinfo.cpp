/**
 * @file adapters/sysinfo.cpp
 * @brief System-info adapter implementation. The platform-divergent reads live
 *        in platform::read_resources() (behind the translation-unit split), so
 *        this adapter is a thin, injectable seam with no #ifdef of its own.
 */
#include "adapters/sysinfo.h"

#include "common/platform.h"

namespace keur::adapters {

Resources SysInfo::default_read() {
    platform::Resources p = platform::read_resources();
    Resources r;
    r.load1 = p.load1;
    r.ncpu = p.ncpu;
    r.ram_total_gb = p.ram_total_gb;
    r.ram_free_gb = p.ram_free_gb;
    return r;
}

}  // namespace keur::adapters
