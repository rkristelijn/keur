/**
 * @file cmd/doctor.h
 * @brief keur doctor — dependency health check: which proven tools are present,
 *        what is missing, and how to fix it, with a verdict + exit code.
 *
 * Answers "can keur actually do its job on this machine?" — NOT "why did this
 * rule (not) fire" (that is `rules list` / `difficulty --explain`). doctor is
 * the dependency-management brick (roadmap T-104): for every ENABLED scanner it
 * reports PATH availability and, when missing, the OS-specific install hint. A
 * missing ENABLED scanner means the gate runs silently incomplete, so doctor
 * exits non-zero — a disabled or opt-in tool missing is fine.
 *
 * Orchestrator only (ADR-003): reads the scanner inventory via the pure
 * config/scanners module through injected probes; the render + verdict are
 * pure functions, unit-tested with a spoofed probe. @id KEUR-DOCTOR
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "config/scanners.h"

namespace keur::cmd {

/// Parsed `keur doctor` options.
struct DoctorOptions {
    bool json = false;      ///< --json machine-readable output
    std::string config;     ///< --config explicit keur.toml path ("" = resolve)
};

/// Injectable probes for doctor (defaults = real PATH check). Spoofable in tests.
struct DoctorDeps {
    std::function<bool(const std::string& tool)> tool_on_path;  ///< is tool on PATH?
};

/// One tool's resolved health line (pure data; render/verdict derive from it).
struct DoctorRow {
    std::string name;          ///< scanner name
    std::string group;         ///< secrets | sast | deps | iac | pii
    bool        enabled = true;///< enabled in config
    bool        on_path = false;///< resolved on PATH
    std::string install_hint;  ///< non-empty only when enabled && !on_path && hint known
};

/// Parse `--json` / `--config <path>` from argv.
DoctorOptions doctor_parse(const std::vector<std::string>& args);

/// Build the health rows from the resolved scanner inventory. Pure.
[[nodiscard]] std::vector<DoctorRow> doctor_rows(const std::vector<config::Scanner>& scanners);

/// Overall exit code: 0 when every ENABLED scanner is on PATH, else 1. Pure.
[[nodiscard]] int doctor_exit_code(const std::vector<DoctorRow>& rows);

/// Render rows as human text (one line per tool + a verdict line). Pure.
[[nodiscard]] std::vector<std::string> doctor_render_text(const std::vector<DoctorRow>& rows);

/// Render rows as a single JSON object string (tools + verdict). Pure.
[[nodiscard]] std::string doctor_render_json(const std::vector<DoctorRow>& rows);

/// Run doctor. Returns the health exit code (0 healthy, 1 degraded).
int doctor(const DoctorOptions& opts, const DoctorDeps& deps = DoctorDeps{});

}  // namespace keur::cmd
