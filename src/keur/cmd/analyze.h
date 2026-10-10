/**
 * @file cmd/analyze.h
 * @brief keur analyze — read-only, non-blocking code analyses (ADR-058/060).
 *
 * A family of aggregating analyses, distinct from the gate (scan/check).
 * First member: `literals` — extract string literals, count them, classify
 * into noise/magic/identity, and report what is worth centralising (magic,
 * rule of three) or externalising (identity: ids, emails, hosts, paths).
 *
 * Always exits 0 — analyze informs, it never blocks a commit.
 */
#pragma once
#include <string>
#include <vector>

namespace keur::cmd {

struct AnalyzeOptions {
  std::string kind;            // "literals" (first analysis)
  std::string target = ".";    // directory or file
  bool magic_only = false;     // --magic : only the rule-of-three bucket
  bool identity_only = false;  // --identity : only the externalise bucket
  int min_count = 1;           // --min-count N : hide literals seen fewer times
};

/** Parse `<kind> [path] [flags]` from argv. */
AnalyzeOptions analyze_parse(const std::vector<std::string>& args);

/** Run the analyze subcommand. Returns 0 (never blocks). */
int analyze(const AnalyzeOptions& opts);

}  // namespace keur::cmd
