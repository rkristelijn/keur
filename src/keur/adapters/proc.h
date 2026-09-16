/**
 * @file proc.h
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
 */
#pragma once
#include <functional>
#include <string>

namespace keur {

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
     * Run a command and capture stdout + exit code.
     * Used by external-scanner adapters (libmode).
     */
    std::function<ProcResult(const std::string& cmd)> capture = default_capture;

    /** True if a binary is resolvable on PATH. */
    std::function<bool(const std::string& bin)> has = default_has;

    // Real implementations.
    static int         default_run(const std::string& cmd);
    static ProcResult  default_capture(const std::string& cmd);
    static bool        default_has(const std::string& bin);
};

}  // namespace keur
