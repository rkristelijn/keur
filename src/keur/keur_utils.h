/**
 * @file keur_utils.h
 * @brief Shared helpers used across keur subcommands.
 *
 * Keeping these in one place prevents duplication between cmd_check.cpp,
 * cmd_scan.cpp, and future subcommands.
 */
#pragma once
#include <cstdlib>
#include <string>
#include <vector>

namespace keur {

/**
 * JSON-escape a string (backslash, double-quote, newline, tab).
 * Matches the escaping used by keur-rules/main.cpp.
 */
inline std::string jesc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"':  o += "\\\""; break;
            case '\n': o += ' ';    break;
            case '\t': o += ' ';    break;
            default:   o += c;
        }
    }
    return o;
}

/**
 * Return the list of rule directories to search.
 *
 * Resolution order:
 *   1. $KEUR_HOME/rules
 *   2. Each colon-separated entry in $KEUR_RULES_DIRS
 */
inline std::vector<std::string> rule_dirs() {
    std::vector<std::string> dirs;
    if (const char* home = std::getenv("KEUR_HOME"))
        dirs.push_back(std::string(home) + "/rules");
    if (const char* extra = std::getenv("KEUR_RULES_DIRS")) {
        std::string s(extra), cur;
        for (char c : s) {
            if (c == ':') {
                if (!cur.empty()) { dirs.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) dirs.push_back(cur);
    }
    return dirs;
}

}  // namespace keur
