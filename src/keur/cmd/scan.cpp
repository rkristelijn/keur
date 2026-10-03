/**
 * @file cmd/scan.cpp
 * @brief keur scan — run the deterministic scanner pipeline over a directory.
 *
 * SRP: this file only orchestrates scan + emit. The findings-to-output logic
 * lives in the report provider (report.h); rule dirs in keur_utils.h.
 */
#include "cmd/scan.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "rules/rule_engine.h"
#include "config/ruleset.h"
#include "util/keur_utils.h"
#include "report/report.h"

namespace keur::cmd {

ScanOptions scan_parse(const std::vector<std::string>& args) {
    ScanOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--format" && i + 1 < args.size())
            o.format = report::parse_format(args[++i]);
        else if (args[i] == "--color" && i + 1 < args.size())
            o.color = report::parse_color(args[++i]);
        else if (args[i] == "--json")  // back-compat alias for --format jsonl
            o.format = report::OutputFormat::Jsonl;
        else if (args[i] == "--config" && i + 1 < args.size())
            o.config = args[++i];
        else if (args[i][0] != '-')
            o.target = args[i];
    }
    return o;
}

int scan(const ScanOptions& opts) {
    std::vector<Rule> rules = keur::config::load_active_rules();

    if (rules.empty()) {
        std::cerr << "keur scan: no rules found "
                     "(set KEUR_HOME or KEUR_RULES_DIRS)\n";
        return 0;
    }

    // A target may be a single file or a directory. rules_scan_dir walks a
    // tree; for a single file we must use rules_scan_files, otherwise the file
    // is treated as a (childless) directory and nothing is scanned.
    std::error_code ec;
    std::vector<RuleFinding> findings;
    if (std::filesystem::is_regular_file(opts.target, ec)) {
        findings = rules_scan_files(rules, {opts.target});
    } else {
        findings = rules_scan_dir(rules, opts.target);
    }

    // Single output touch point — the report provider owns the format.
    report::ReportOptions ro;
    ro.format = opts.format;
    ro.color  = opts.color;
    report::report_findings(std::cout, findings, ro);

    std::cerr << "keur scan: " << findings.size() << " finding(s) in "
              << opts.target << "\n";
    return 0;
}

}  // namespace keur::cmd
