/**
 * @file cmd/doctor.cpp
 * @brief keur doctor — dependency health check (roadmap T-104).
 *
 * Orchestrator: resolve the scanner inventory from keur.toml (falling back to
 * the baked default set), probe PATH, then render + verdict. The row-building,
 * verdict and rendering are pure functions, unit-tested with a spoofed probe.
 * @implements ADR-003 @proves KEUR-DOCTOR
 */
#include "cmd/doctor.h"

#include <iostream>

#include "cmd/policy.h"       // resolve_config
#include "common/platform.h"  // tool_on_path
#include "util/keur_utils.h"  // jesc

namespace keur::cmd {

DoctorOptions doctor_parse(const std::vector<std::string>& args) {
    DoctorOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json") o.json = true;
        else if (args[i] == "--config" && i + 1 < args.size()) o.config = args[++i];
    }
    return o;
}

// ---------------------------------------------------------------------------
// Pure core
// ---------------------------------------------------------------------------

std::vector<DoctorRow> doctor_rows(const std::vector<config::Scanner>& scanners) {
    std::vector<DoctorRow> rows;
    for (const auto& s : scanners) {
        DoctorRow r;
        r.name = s.name;
        r.group = s.group;
        r.enabled = s.enabled;
        r.on_path = s.on_path;
        // A hint only helps when the tool is needed (enabled) but absent.
        if (s.enabled && !s.on_path) r.install_hint = config::install_hint(s.name);
        rows.push_back(std::move(r));
    }
    return rows;
}

int doctor_exit_code(const std::vector<DoctorRow>& rows) {
    for (const auto& r : rows)
        if (r.enabled && !r.on_path) return 1;  // an enabled gate is silently incomplete
    return 0;
}

std::vector<std::string> doctor_render_text(const std::vector<DoctorRow>& rows) {
    std::vector<std::string> out;
    out.emplace_back("keur doctor — scanner dependency health");

    int missing = 0;
    for (const auto& r : rows) {
        std::string status;
        if (!r.enabled)        status = "off  ";   // disabled in config
        else if (r.on_path)    status = "OK   ";
        else { status = "MISS "; ++missing; }

        std::string pad(r.name.size() < 12 ? 12 - r.name.size() : 1, ' ');
        std::string line = "  " + status + r.name + pad + "[" + r.group + "]";
        if (r.enabled && !r.on_path) {
            line += r.install_hint.empty()
                        ? "  not on PATH — see the tool's own install docs"
                        : "  not on PATH — fix: " + r.install_hint;
        }
        out.push_back(line);
    }

    if (missing == 0)
        out.emplace_back("verdict: HEALTHY — every enabled scanner is available");
    else
        out.emplace_back("verdict: DEGRADED — " + std::to_string(missing) +
                         " enabled scanner(s) missing; the gate runs incomplete");
    return out;
}

std::string doctor_render_json(const std::vector<DoctorRow>& rows) {
    std::string s = "{\"tools\":[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (i) s += ",";
        s += "{\"name\":\"" + jesc(r.name) + "\",";
        s += "\"group\":\"" + jesc(r.group) + "\",";
        s += "\"enabled\":"; s += (r.enabled ? "true" : "false"); s += ",";
        s += "\"on_path\":"; s += (r.on_path ? "true" : "false"); s += ",";
        s += "\"install_hint\":\"" + jesc(r.install_hint) + "\"}";
    }
    s += "],";
    int code = doctor_exit_code(rows);
    s += "\"verdict\":\"" + std::string(code == 0 ? "healthy" : "degraded") + "\"}";
    return s;
}

// ---------------------------------------------------------------------------
// Orchestrator
// ---------------------------------------------------------------------------

int doctor(const DoctorOptions& opts, const DoctorDeps& deps) {
    // Resolve declared scanners from the config stack; fall back to the baked
    // default set so doctor is never empty (same idea as the manager board).
    std::string toml = resolve_config(opts.config);
    auto declared = config::parse_scanners(toml);
    if (declared.empty()) declared = config::default_scanners();

    config::ScannerProbe probe;
    probe.tool_on_path = deps.tool_on_path
                             ? deps.tool_on_path
                             : [](const std::string& t) { return keur::platform::tool_on_path(t); };
    probe.exists = [](const std::string&) { return false; };  // doctor ignores dotfiles
    auto resolved = config::resolve_scanners(declared, probe);

    auto rows = doctor_rows(resolved);

    if (opts.json) std::cout << doctor_render_json(rows) << "\n";
    else for (const auto& line : doctor_render_text(rows)) std::cout << line << "\n";

    return doctor_exit_code(rows);
}

}  // namespace keur::cmd
