/**
 * @file adapters/proc.cpp
 * @brief Process adapter implementation — the only place keur spawns external
 *        tools (side effects + captured runs). All OS-divergent primitives
 *        (spawn, capture, exit-code decode, PATH lookup) come from platform::,
 *        so this file has no #ifdef and no POSIX-only shell constructs.
 */
#include "adapters/proc.h"

#include <string>

#include "common/platform.h"

namespace keur::adapters {

int Proc::default_run(const std::string& cmd) {
    return platform::run(cmd);
}

int Proc::default_run_argv(
    const std::vector<std::string>& argv,
    const std::vector<std::pair<std::string, std::string>>& env) {
    return platform::run_argv(argv, env);
}

ProcResult Proc::default_capture(const std::string& cmd) {
    ProcResult r;
    auto cap = platform::capture(cmd);
    r.exit_code = cap.exit_code;
    r.stdout_text = std::move(cap.out);
    return r;
}

bool Proc::default_has(const std::string& bin) {
    return platform::tool_on_path(bin);
}

}  // namespace keur::adapters
