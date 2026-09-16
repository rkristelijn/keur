/**
 * @file cmd_scan.cpp
 * @brief keur scan — run the deterministic scanner pipeline over a directory.
 *
 * SRP: this file only orchestrates scan + emit. The findings-to-output logic
 * lives in the report provider (report.h); rule dirs in keur_utils.h.
 */
#include "cmd_scan.h"

#include <iostream>
#include <string>
#include <vector>

#include "../rules/rule_engine.h"
#include "keur_utils.h"
#include "report.h"

namespace keur {

ScanOptions cmd_scan_parse(const std::vector<std::string>& args) {
    ScanOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json")                              o.json   = true;
        else if (args[i] == "--config" && i + 1 < args.size()) o.config = args[++i];
        else if (args[i][0] != '-')                           o.target = args[i];
    }
    return o;
}

int cmd_scan(const ScanOptions& opts) {
    std::vector<Rule> rules;
    for (auto& d : rule_dirs()) {
        auto rs = rules_load(d);
        for (auto& r : rs) rules.push_back(std::move(r));
    }

    if (rules.empty()) {
        std::cerr << "keur scan: no rules found "
                     "(set KEUR_HOME or KEUR_RULES_DIRS)\n";
        return 0;
    }

    auto findings = rules_scan_dir(rules, opts.target);

    // Single output touch point — the report provider owns the format.
    ReportOptions ro;
    ro.format = OutputFormat::Jsonl;  // scan defaults to machine-readable
    report_findings(std::cout, findings, ro);

    std::cerr << "keur scan: " << findings.size() << " finding(s) in "
              << opts.target << "\n";
    return 0;
}

}  // namespace keur
