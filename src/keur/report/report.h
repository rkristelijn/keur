/**
 * @file report.h
 * @brief Output / reporting provider — the single touch point for findings output.
 *
 * Every finding that reaches a user goes through here. One place owns:
 *   - the JSON shape (so keur-rules and keur scan never diverge again),
 *   - the output format (jsonl / compact / verbose),
 *   - colour (themeable, TTY-aware).
 *
 * @see docs/adr/ADR-003-provider-architecture.md
 */
#pragma once
#include <ostream>
#include <string>
#include <vector>

#include "rules/rule_engine.h"

namespace keur {

/** How findings are rendered. */
enum class OutputFormat {
    Jsonl,    // one JSON object per line (machine-readable, default for scan)
    Compact,  // one short line per finding (file:line RULE message)
    Verbose,  // multi-line, human-friendly, with fix hints
};

/** Colour policy for human formats. */
enum class ColorMode { Auto, Always, Never };

/** Reporting configuration. */
struct ReportOptions {
    OutputFormat format = OutputFormat::Jsonl;
    ColorMode    color  = ColorMode::Auto;
};

/**
 * Serialise a single finding to one JSON line (no trailing newline).
 * This is THE definition of keur's finding JSON — used by every emitter.
 */
std::string finding_to_json(const RuleFinding& f);

/**
 * Write findings to `out` in the requested format.
 * Returns the number of findings written.
 */
std::size_t report_findings(std::ostream& out,
                            const std::vector<RuleFinding>& findings,
                            const ReportOptions& opts);

/** Resolve whether colour should be used, honouring ColorMode + TTY + NO_COLOR. */
bool use_color(ColorMode mode, bool is_tty);

}  // namespace keur
