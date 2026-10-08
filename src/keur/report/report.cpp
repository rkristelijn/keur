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
    // Auto: NO_COLOR wins over everything (no-color.org). Otherwise an explicit
    // force env (FORCE_COLOR / CLICOLOR_FORCE — the Node and BSD conventions)
    // turns colour on even without a TTY, so colour survives a pipe, a git hook,
    // or a terminal recording. Falling through, colour only on a real TTY.
    if (std::getenv("NO_COLOR") != nullptr) return false;
    if (const char* f = std::getenv("FORCE_COLOR"); f && *f && std::string(f) != "0")
        return true;
    if (const char* c = std::getenv("CLICOLOR_FORCE"); c && *c && std::string(c) != "0")
        return true;
    return is_tty;
}

OutputFormat parse_format(const std::string& s, OutputFormat fallback) {
    if (s == "jsonl")   return OutputFormat::Jsonl;
    if (s == "compact") return OutputFormat::Compact;
    if (s == "verbose") return OutputFormat::Verbose;
    if (s == "sarif")   return OutputFormat::Sarif;
    return fallback;
}

ColorMode parse_color(const std::string& s, ColorMode fallback) {
    if (s == "auto")   return ColorMode::Auto;
    if (s == "always") return ColorMode::Always;
    if (s == "never")  return ColorMode::Never;
    return fallback;
}

std::size_t report_findings(std::ostream& out,
                            const std::vector<RuleFinding>& findings,
                            const ReportOptions& opts) {
    bool color = use_color(opts.color, false);  // ostream is not a tty here

    // SARIF is a single document wrapping all results (GitHub/GitLab code
    // scanning ingest it), so it is emitted whole rather than per finding.
    if (opts.format == OutputFormat::Sarif) {
        auto sarif_level = [](const std::string& sev) -> const char* {
            if (sev == "error" || sev == "critical") return "error";
            if (sev == "warning") return "warning";
            return "note";
        };
        out << "{\"version\":\"2.1.0\","
               "\"$schema\":\"https://json.schemastore.org/sarif-2.1.0.json\","
               "\"runs\":[{\"tool\":{\"driver\":{\"name\":\"keur\","
               "\"informationUri\":\"https://github.com/rkristelijn/keur\","
               "\"rules\":[]}},\"results\":[";
        for (std::size_t i = 0; i < findings.size(); ++i) {
            const auto& f = findings[i];
            if (i) out << ",";
            out << "{\"ruleId\":\"" << jesc(f.rule_id) << "\","
                << "\"level\":\"" << sarif_level(f.severity) << "\","
                << "\"message\":{\"text\":\"" << jesc(f.message) << "\"},"
                << "\"locations\":[{\"physicalLocation\":{"
                << "\"artifactLocation\":{\"uri\":\"" << jesc(f.file) << "\"},"
                << "\"region\":{\"startLine\":" << (f.line > 0 ? f.line : 1) << "}}}]}";
        }
        out << "]}]}\n";
        return findings.size();
    }

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

            case OutputFormat::Sarif:
                break;  // handled as a whole document above the loop
        }
    }
    return findings.size();
}

}  // namespace keur::report
