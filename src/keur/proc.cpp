/**
 * @file proc.cpp
 * @brief Process adapter implementation — the only place keur spawns external
 *        tools (side effects + captured runs).
 */
#include "proc.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace keur {

int Proc::default_run(const std::string& cmd) {
    int rc = std::system(cmd.c_str());  // NOLINT(cert-env33-c)
    // Normalise to the process exit code where possible.
#ifdef WEXITSTATUS
    if (rc != -1) return WEXITSTATUS(rc);
#endif
    return rc;
}

ProcResult Proc::default_capture(const std::string& cmd) {
    ProcResult r;
    std::string full = cmd + " 2>/dev/null";
    FILE* p = popen(full.c_str(), "r");
    if (!p) { r.exit_code = -1; return r; }
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) r.stdout_text += buf;
    int rc = pclose(p);
#ifdef WEXITSTATUS
    r.exit_code = (rc == -1) ? -1 : WEXITSTATUS(rc);
#else
    r.exit_code = rc;
#endif
    return r;
}

bool Proc::default_has(const std::string& bin) {
    // `command -v` is POSIX and quieter than `which`.
    std::string cmd = "command -v " + bin + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;  // NOLINT(cert-env33-c)
}

}  // namespace keur
