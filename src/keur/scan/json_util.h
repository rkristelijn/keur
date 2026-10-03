/**
 * @file scan/json_util.h
 * @brief Minimal, internal JSON field extraction for scanner output mapping.
 *        NOT a general JSON parser — keur maps a fixed, known tool schema
 *        (gitleaks/semgrep/shellcheck), so a targeted field/object extractor is
 *        enough and keeps the RE2/regex-first posture (ADR-001). Kept in one
 *        place so both the mappers and the project-local JSONL parser share it.
 * @implements KEUR-SCAN-DISPATCH
 */
#pragma once
#include <string>
#include <vector>

namespace keur::scan::detail {

/// Extract a string field ("key": "value") from a JSON slice (RE2). Escapes in
/// the value are returned as-is (we display/route the text, never re-parse it).
[[nodiscard]] std::string json_str(const std::string& slice, const std::string& key);

/// Extract a numeric field ("key": 123) from a JSON slice. 0 when absent.
[[nodiscard]] int json_int(const std::string& slice, const std::string& key);

/// Split a JSON text into top-level object slices ({...}) starting at `from`,
/// via brace matching that respects string literals. Used to walk the flat
/// array (gitleaks) or the results[] region (semgrep/shellcheck).
[[nodiscard]] std::vector<std::string> object_slices(const std::string& json,
                                                     std::size_t from = 0);

}  // namespace keur::scan::detail
