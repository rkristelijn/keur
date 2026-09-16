/**
 * @file report/report.cpp
 * @brief Output / reporting provider implementation.
 */
#include "report/report.h"

#include <cstdlib>
#include <ostream>
#include <string>
#include <vector>

#include "util/keur_utils.h"

namespace keur::report {

// ANSI colour codes (only emitted when use_color() says so).
namespace {
constexpr const char* C_RED  = "\033[31m";
constexpr const char* C_YEL  = "\033[33m";
constexpr const char* C_DIM  = "\033[90m";
constexpr const char* C_RST  = "\033[0m";

// Map an action to a severity colour.
const char* action_color(const std::string& action) {
    if (action == "block") return C_RED;
    if (action == "warn")  return C_YEL;
    return C_DIM;
}
}  // namespace

std::string finding_to_json(const RuleFinding& f) {
    std::string o;
    o += "{\"source\":\"keur-rules\"";
    o += ",\"rule\":\""      + jesc(f.rule_id)  + "\"";
    o += ",\"title\":\""     + jesc(f.title)    + "\"";
    o += ",\"category\":\""  + jesc(f.category) + "\"";
    o += ",\"severity\":\""  + jesc(f.severity) + "\"";
    o += ",\"action\":\""    + jesc(f.action)   + "\"";
    o += ",\"confidence\":"  + (f.confidence.empty() ? std::string("0.5") : f.confidence);
    o += ",\"deterministic\":true";
    o += ",\"file\":\""      + jesc(f.file)     + "\"";
    o += ",\"line\":"        + std::to_string(f.line);
    o += ",\"message\":\""   + jesc(f.message)  + "\"}";
    return o;
}

bool use_color(ColorMode mode, bool is_tty) {
    if (mode == ColorMode::Never)  return false;
    if (mode == ColorMode::Always) return true;
    // Auto: colour only on a TTY and when NO_COLOR is unset.
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return is_tty;
}

std::size_t report_findings(std::ostream& out,
                            const std::vector<RuleFinding>& findings,
                            const ReportOptions& opts) {
    bool color = use_color(opts.color, false);  // ostream is not a tty here

    for (const auto& f : findings) {
        switch (opts.format) {
            case OutputFormat::Jsonl:
                out << finding_to_json(f) << "\n";
                break;

            case OutputFormat::Compact: {
                const char* col = color ? action_color(f.action) : "";
                const char* rst = color ? C_RST : "";
                out << col << f.file << ":" << f.line << " "
                    << f.rule_id << rst << "  " << f.message << "\n";
                break;
            }

            case OutputFormat::Verbose: {
                const char* col = color ? action_color(f.action) : "";
                const char* rst = color ? C_RST : "";
                out << col << "[" << f.action << "] " << f.rule_id
                    << rst << " - " << f.title << "\n";
                out << "  " << f.file << ":" << f.line << "\n";
                out << "  " << f.message << "\n";
                if (!f.confidence.empty())
                    out << "  confidence: " << f.confidence << "\n";
                out << "\n";
                break;
            }
        }
    }
    return findings.size();
}

}  // namespace keur::report
