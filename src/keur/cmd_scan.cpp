/**
 * @file cmd_scan.cpp
 * @brief keur scan — run the deterministic scanner pipeline over a directory.
 *
 * SRP: this file only orchestrates scan + print.
 * jesc() and rule_dirs() come from keur_utils.h (single definition).
 */
#include "cmd_scan.h"

#include <iostream>
#include <string>
#include <vector>

#include "../rules/rule_engine.h"
#include "keur_utils.h"

namespace keur {

// ---------------------------------------------------------------------------
// Arg parser
// ---------------------------------------------------------------------------

ScanOptions cmd_scan_parse(const std::vector<std::string>& args) {
    ScanOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json")                              o.json   = true;
        else if (args[i] == "--config" && i + 1 < args.size()) o.config = args[++i];
        else if (args[i][0] != '-')                           o.target = args[i];
    }
    return o;
}

// ---------------------------------------------------------------------------
// Orchestrator
// ---------------------------------------------------------------------------

int cmd_scan(const ScanOptions& opts) {
    // Load rules from all configured directories
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

    // Scan directory in-process
    auto findings = rules_scan_dir(rules, opts.target);

    // Emit JSONL — same shape as keur-rules output
    for (auto& f : findings) {
        std::cout << "{\"source\":\"keur-rules\""
                  << ",\"rule\":\""      << jesc(f.rule_id)  << "\""
                  << ",\"title\":\""     << jesc(f.title)    << "\""
                  << ",\"category\":\"" << jesc(f.category) << "\""
                  << ",\"severity\":\"" << jesc(f.severity) << "\""
                  << ",\"action\":\""   << jesc(f.action)   << "\""
                  << ",\"confidence\":" << (f.confidence.empty() ? "0.5" : f.confidence)
                  << ",\"deterministic\":true"
                  << ",\"file\":\""     << jesc(f.file)     << "\""
                  << ",\"line\":"       << f.line
                  << ",\"message\":\""  << jesc(f.message)  << "\"}"
                  << "\n";
    }

    std::cerr << "keur scan: " << findings.size() << " finding(s) in "
              << opts.target << "\n";
    return 0;
}

}  // namespace keur
