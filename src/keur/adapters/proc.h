/**
 * @file adapters/proc.h
 * @brief Process adapter — the single touch point for spawning external tools.
 *
 * Any external command keur runs (git config side effects now; semgrep,
 * gitleaks, trivy in libmode later) goes through this adapter. Centralising it:
 *   - one implementation of spawn + capture (one quoting/timeout policy),
 *   - a stable seam for tests (inject a fake Proc — nothing is executed),
 *   - no ad-hoc system()/popen() for tools scattered across the code.
 *
 * Note: pure git *queries* live in the Git adapter (git.h). Proc is for
 * fire-and-forget side effects and, later, external-scanner invocation.
 *
 * @see docs/adr/ADR-003-provider-architecture.md
 * @implements ADR-003
 */
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace keur::adapters {

/** Result of running an external command. */
struct ProcResult {
    int         exit_code = 0;
    std::string stdout_text;
};

/**
 * Process operations. Implemented once in proc.cpp; tests inject fakes.
 */
struct Proc {
    /**
     * Run a shell command, discard output, return the exit code.
     * Used for fire-and-forget side effects (e.g. git config --global).
     */
    std::function<int(const std::string& cmd)> run = default_run;

    /**
     * Run a program shell-free from an argv vector (no /bin/sh, no cmd.exe),
     * with optional KEY=value environment overrides. Preferred over run() when
     * any argument is constructed from non-literal input — nothing in argv can
     * be interpreted as a shell metacharacter (CWE-78). @see KEUR-SEC-006.
     */
    std::function<int(const std::vector<std::string>& argv,
                      const std::vector<std::pair<std::string, std::string>>& env)>
        run_argv = default_run_argv;

    /**
     * Run a command and capture stdout + exit code.
     * Used by external-scanner adapters (libmode).
     */
    std::function<ProcResult(const std::string& cmd)> capture = default_capture;

    /**
     * Shell-free capture: spawn argv[0] (PATH-resolved) with literal arguments,
     * NO /bin/sh, and capture stdout. The safe form for scanner invocation
     * where a repo-local config path or staged file name is passed as an
     * argument — nothing in argv can be reinterpreted as a shell metacharacter
     * (CWE-78, KEUR-SEC-006). Preferred over capture() for external scanners.
     */
    std::function<ProcResult(const std::vector<std::string>& argv)>
        capture_argv = default_capture_argv;

    /** True if a binary is resolvable on PATH. */
    std::function<bool(const std::string& bin)> has = default_has;

    // Real implementations.
    static int         default_run(const std::string& cmd);
    static int         default_run_argv(
        const std::vector<std::string>& argv,
        const std::vector<std::pair<std::string, std::string>>& env);
    static ProcResult  default_capture(const std::string& cmd);
    static ProcResult  default_capture_argv(const std::vector<std::string>& argv);
    static bool        default_has(const std::string& bin);
};

}  // namespace keur::adapters
