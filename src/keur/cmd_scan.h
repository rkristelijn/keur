/**
 * @file cmd_scan.h
 * @brief keur scan — run the deterministic scanner pipeline over a directory.
 *
 * Full-repo / ad-hoc scanning.  Walks the target path, runs the rule engine
 * in-process, and prints findings as JSONL to stdout (same format as
 * keur-rules).
 */
#pragma once
#include <string>
#include <vector>

namespace keur {

struct ScanOptions {
  std::string target = ".";   // directory or file to scan
  std::string config;         // path to keur.toml (empty = auto-resolve)
  bool        json   = false; // force JSON output even when writing to a tty
};

/** Parse [path] and flags from argv. */
ScanOptions cmd_scan_parse(const std::vector<std::string>& args);

/**
 * Run the scan subcommand.
 * Prints findings as JSONL to stdout.
 * Returns 0 always (scan never blocks; use `keur check` for gating).
 */
int cmd_scan(const ScanOptions& opts);

}  // namespace keur
