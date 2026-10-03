/**
 * @file scan/mappers.cpp
 * @brief Per-tool JSON → Finding mappers (ADR-019). Each is a pure string →
 *        vector transform, independently testable with a captured sample of the
 *        tool's output. Split from dispatch.cpp so the orchestration stays small
 *        (one job per file, AGENTS.md). @implements KEUR-SCAN-DISPATCH
 */
#include "scan/dispatch.h"

#include <re2/re2.h>

#include <algorithm>
#include <cstdio>

#include "scan/json_util.h"

namespace keur::scan {

using detail::json_int;
using detail::json_str;
using detail::object_slices;

namespace {

// gitleaks findings are secrets: block by default (config secrets group).
constexpr const char* k_gitleaks_action = "block";
constexpr const char* k_gitleaks_confidence = "0.99";  // matches base-confidence
// semgrep is SAST: warn by default (config default-action = warn).
constexpr const char* k_semgrep_action = "warn";
constexpr const char* k_semgrep_confidence = "0.80";

std::string semgrep_severity_to_keur(const std::string& sev) {
    // semgrep: ERROR | WARNING | INFO → keur severity vocabulary.
    if (sev == "ERROR") return "error";
    if (sev == "WARNING") return "warning";
    if (sev == "INFO") return "info";
    return "warning";
}

}  // namespace

std::vector<Finding> map_gitleaks_json(const std::string& json) {
    std::vector<Finding> out;
    // gitleaks --report-format json emits a flat array of finding objects.
    for (const auto& obj : object_slices(json)) {
        Finding f;
        f.source = "gitleaks";
        f.rule = json_str(obj, "RuleID");
        f.title = json_str(obj, "Description");
        if (f.title.empty()) f.title = f.rule;
        f.category = "secret";
        f.severity = "critical";
        f.action = k_gitleaks_action;
        f.confidence = k_gitleaks_confidence;
        f.deterministic = true;
        f.file = json_str(obj, "File");
        f.line = json_int(obj, "StartLine");
        f.message = f.title;
        // Only keep records that carried a rule id — skips array wrappers /
        // stray braces that are not findings.
        if (!f.rule.empty()) out.push_back(std::move(f));
    }
    return out;
}

std::vector<Finding> map_semgrep_json(const std::string& json) {
    std::vector<Finding> out;
    // semgrep --json nests findings under "results": [ ... ]. Restrict the
    // brace-splitter to that region so we don't also pick up the "paths"/
    // "errors" objects elsewhere in the document.
    size_t r = json.find("\"results\"");
    if (r == std::string::npos) return out;
    size_t lb = json.find('[', r);
    if (lb == std::string::npos) return out;
    for (const auto& obj : object_slices(json, lb)) {
        std::string check = json_str(obj, "check_id");
        if (check.empty()) continue;
        Finding f;
        f.source = "semgrep";
        f.rule = check;
        f.category = "sast";
        // severity + message live under "extra": {...}; the flat field search
        // finds them anywhere in the record, which is fine — one per record.
        f.severity = semgrep_severity_to_keur(json_str(obj, "severity"));
        f.action = k_semgrep_action;
        f.confidence = k_semgrep_confidence;
        f.deterministic = true;
        f.file = json_str(obj, "path");
        f.line = json_int(obj, "line");   // start.line
        f.message = json_str(obj, "message");
        if (f.message.empty()) f.message = check;
        f.title = check;
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<Finding> map_shellcheck_json(const std::string& json) {
    std::vector<Finding> out;
    // shellcheck --format=json1 → {"comments":[{file,line,level,code,message}]}.
    size_t c = json.find("\"comments\"");
    if (c == std::string::npos) return out;
    size_t lb = json.find('[', c);
    if (lb == std::string::npos) return out;
    for (const auto& obj : object_slices(json, lb)) {
        int code = json_int(obj, "code");
        if (code == 0) continue;  // not a comment record
        Finding f;
        f.source = "shellcheck";
        f.rule = "SC" + std::to_string(code);
        f.category = "quality";
        std::string level = json_str(obj, "level");  // error|warning|info|style
        // Only a hard shellcheck "error" blocks; warnings/info/style WARN so a
        // lint code (e.g. SC2034 unused var) never overblocks a commit.
        f.action = (level == "error") ? "block" : "warn";
        f.severity = level.empty() ? "warning" : level;
        f.confidence = "0.90";  // deterministic linter
        f.deterministic = true;
        f.file = json_str(obj, "file");
        f.line = json_int(obj, "line");
        f.message = json_str(obj, "message");
        if (f.message.empty()) f.message = f.rule;
        f.title = f.rule;
        out.push_back(std::move(f));
    }
    return out;
}

namespace {

// Parse the (possibly numeric, possibly quoted) confidence out of a JSONL line
// and clamp it to `cap`. Extracted so parse_finding_jsonl stays flat (one job
// per helper, keeps nesting shallow — KEUR-QUAL-004).
std::string capped_confidence(const std::string& line, double cap) {
    std::string cv = json_str(line, "confidence");
    if (cv.empty()) {
        // numeric (unquoted) confidence.
        RE2 re("\"confidence\"\\s*:\\s*([0-9]*\\.?[0-9]+)");
        std::string m;
        if (RE2::PartialMatch(line, re, &m)) cv = m;
    }
    double conf = cap;
    if (!cv.empty()) {
        try { conf = std::min(std::stod(cv), cap); } catch (...) { conf = cap; }
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", conf);
    return buf;
}

}  // namespace

std::vector<Finding> parse_finding_jsonl(const std::string& jsonl, double cap) {
    std::vector<Finding> out;
    size_t pos = 0;
    while (pos < jsonl.size()) {
        size_t nl = jsonl.find('\n', pos);
        std::string line =
            (nl == std::string::npos) ? jsonl.substr(pos) : jsonl.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? jsonl.size() : nl + 1;
        if (line.find('{') == std::string::npos) continue;  // blank / non-JSON
        std::string rule = json_str(line, "rule");
        std::string action = json_str(line, "action");
        if (rule.empty() && action.empty()) continue;  // not a finding line
        Finding f;
        f.source = "project-local";
        f.rule = rule.empty() ? "project-local" : rule;
        f.title = json_str(line, "title");
        if (f.title.empty()) f.title = f.rule;
        f.category = json_str(line, "category");
        if (f.category.empty()) f.category = "project";
        f.severity = json_str(line, "severity");
        if (f.severity.empty()) f.severity = "warning";
        f.action = action.empty() ? "warn" : action;
        f.file = json_str(line, "file");
        f.line = json_int(line, "line");
        f.message = json_str(line, "message");
        if (f.message.empty()) f.message = f.title;
        // Confidence cap: a project script may not claim more weight than a
        // calibrated proven tool (ADR-019).
        f.confidence = capped_confidence(line, cap);
        out.push_back(std::move(f));
    }
    return out;
}

}  // namespace keur::scan
