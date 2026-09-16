/**
 * @file cmd/scan.h
 * @brief keur scan — run the deterministic scanner pipeline over a directory.
 *
 * Full-repo / ad-hoc scanning.  Walks the target path, runs the rule engine
 * in-process, and prints findings as JSONL to stdout (same format as
 * keur-rules).
 */
#pragma once
#include <string>
#include <vector>

#include "report/report.h"

namespace keur::cmd {

struct ScanOptions {
  std::string        target = ".";     // directory or file to scan
  std::string        config;           // path to keur.toml (empty = auto-resolve)
  report::OutputFormat format = report::OutputFormat::Jsonl;  // --format
  report::ColorMode    color  = report::ColorMode::Auto;      // --color
};

/** Parse [path] and flags from argv. */
ScanOptions scan_parse(const std::vector<std::string>& args);

/**
 * Run the scan subcommand.
 * Prints findings as JSONL to stdout.
 * Returns 0 always (scan never blocks; use `keur check` for gating).
 */
int scan(const ScanOptions& opts);

}  // namespace keur::cmd
