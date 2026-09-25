/**
 * @file config/scanners.cpp
 * @brief Pure scanner inventory: parse [scanners.*], map known dotfiles, and
 *        resolve PATH + file presence via an injected probe. No I/O of its own.
 *        @implements KEUR-SCANNERS
 */
#include "config/scanners.h"

#include "common/platform.h"

namespace keur::config {

namespace {

// Trim ASCII whitespace from both ends.
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Strip an enclosing pair of identical quotes and any inline comment.
std::string clean_value(std::string v) {
    // remove inline comment (after unquoted #)
    if (!v.empty() && v.front() != '"' && v.front() != '\'') {
        size_t h = v.find('#');
        if (h != std::string::npos) v = v.substr(0, h);
    }
    v = trim(v);
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
        v = v.substr(1, v.size() - 2);
    return v;
}

// Known config/ignore files per scanner (data-driven — a new tool is one row).
struct ConfigFileMap {
    const char* scanner;
    const char* files;   // space-separated repo-relative paths
};

const ConfigFileMap k_config_files[] = {
    {"gitleaks",   ".gitleaks.toml .gitleaksignore"},
    {"trufflehog", ".trufflehogignore"},
    {"semgrep",    ".semgrep.yml .semgrepignore"},
    {"trivy",      "trivy.yaml .trivyignore"},
    {"grype",      ".grype.yaml"},
    {"osv-scanner", "osv-scanner.toml"},
    {"checkov",    ".checkov.yaml .checkov.yml"},
    {"tfsec",      ".tfsec.yml tfsec.yml"},
};

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}  // namespace

std::vector<std::string> known_config_files(const std::string& scanner_name) {
    for (const auto& m : k_config_files)
        if (scanner_name == m.scanner) return split_ws(m.files);
    return {};
}

namespace {
// Per-tool install packages/commands. brew (macOS), apt (Debian/Ubuntu),
// apk (Alpine). A "" field means no simple package on that OS (fall back to the
// tool's own installer/docs). Data-driven — a new tool is one row.
struct InstallMap {
    const char* scanner;
    const char* brew;   // `brew install <brew>`
    const char* apt;    // `apt install <apt>`
    const char* apk;    // `apk add <apk>`
};
const InstallMap k_install[] = {
    {"gitleaks",    "gitleaks",    "gitleaks",   "gitleaks"},
    {"trufflehog",  "trufflehog",  "",           ""},
    {"semgrep",     "semgrep",     "",           "py3-pip (then: pip install semgrep)"},
    {"trivy",       "trivy",       "trivy",      "trivy"},
    {"grype",       "grype",       "",           ""},
    {"osv-scanner", "osv-scanner", "",           ""},
    {"checkov",     "checkov",     "",           "py3-pip (then: pip install checkov)"},
    {"tfsec",       "tfsec",       "",           "tfsec"},
    {"cpm",         "",            "",           ""},   // installed from source
};
}  // namespace

std::string install_hint(const std::string& scanner_name) {
    const InstallMap* m = nullptr;
    for (const auto& row : k_install)
        if (scanner_name == row.scanner) { m = &row; break; }
    if (!m) return "";
    switch (platform::os_kind()) {
        case platform::OsKind::MacOS:
            return (m->brew && *m->brew) ? std::string("brew install ") + m->brew : "";
        case platform::OsKind::Linux:
            // Prefer apt; fall back to apk hint when apt has no package.
            if (m->apt && *m->apt) return std::string("apt install ") + m->apt;
            if (m->apk && *m->apk) return std::string("apk add ") + m->apk;
            return "";
        case platform::OsKind::Windows:
        case platform::OsKind::Unknown:
        default:
            // No universal package manager; point at the tool's own docs.
            return "";
    }
}

std::vector<Scanner> default_scanners() {
    // The proven-tool baseline keur ships with (mirrors config/keur.toml's
    // [scanners.*]). Enabled by default except presidio (opt-in higher-fidelity
    // PII). group/default-action match the shipped template.
    struct Def { const char* name; const char* group; const char* action; bool enabled; };
    static const Def k[] = {
        {"gitleaks",    "secrets", "block", true},
        {"trufflehog",  "secrets", "block", true},
        {"trivy",       "deps",    "warn",  true},
        {"grype",       "deps",    "warn",  true},
        {"osv-scanner", "deps",    "warn",  true},
        {"semgrep",     "sast",    "warn",  true},
        {"checkov",     "iac",     "warn",  true},
        {"tfsec",       "iac",     "warn",  true},
        {"presidio",    "pii",     "warn",  false},
    };
    std::vector<Scanner> out;
    for (const auto& d : k) {
        Scanner s;
        s.name = d.name; s.bin = d.name; s.group = d.group;
        s.default_action = d.action; s.enabled = d.enabled;
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<Scanner> parse_scanners(const std::string& toml_body) {
    std::vector<Scanner> scanners;
    Scanner* cur = nullptr;
    std::string line;
    size_t pos = 0;

    auto next_line = [&](std::string& out) -> bool {
        if (pos >= toml_body.size()) return false;
        size_t nl = toml_body.find('\n', pos);
        if (nl == std::string::npos) { out = toml_body.substr(pos); pos = toml_body.size(); }
        else { out = toml_body.substr(pos, nl - pos); pos = nl + 1; }
        return true;
    };

    while (next_line(line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        // Section header: [scanners.<name>] starts a scanner; any other [..]
        // ends the current scanner scope.
        if (t.front() == '[') {
            const std::string prefix = "[scanners.";
            if (t.rfind(prefix, 0) == 0 && t.back() == ']') {
                Scanner s;
                s.name = t.substr(prefix.size(), t.size() - prefix.size() - 1);
                scanners.push_back(std::move(s));
                cur = &scanners.back();
            } else {
                cur = nullptr;  // left the [scanners.*] space
            }
            continue;
        }
        if (!cur) continue;

        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = clean_value(t.substr(eq + 1));
        if (key == "bin") cur->bin = val;
        else if (key == "group") cur->group = val;
        else if (key == "default-action") cur->default_action = val;
        else if (key == "enabled") cur->enabled = (val == "true");
    }
    return scanners;
}

std::vector<std::string> tier_scanners(const std::string& toml_body,
                                       const std::string& tier) {
    // Find the [tiers.<tier>] section, then its `scanners = [ ... ]` array. The
    // array may span lines; we accumulate until the closing ']'. Keeps this a
    // small line scanner rather than a full TOML parser — the same approach as
    // parse_scanners above (keur's config is deliberately simple).
    const std::string header = "[tiers." + tier + "]";
    size_t sec = toml_body.find(header);
    if (sec == std::string::npos) return {};
    size_t key = toml_body.find("scanners", sec);
    if (key == std::string::npos) return {};
    // Guard: the scanners key must belong to THIS tier, not a later section.
    size_t next_sec = toml_body.find("\n[", sec + header.size());
    if (next_sec != std::string::npos && key > next_sec) return {};
    size_t lb = toml_body.find('[', key);
    size_t rb = toml_body.find(']', key);
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) return {};
    std::string body = toml_body.substr(lb + 1, rb - lb - 1);
    // Split on commas, strip quotes/whitespace.
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        std::string v = trim(cur);
        if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
            v = v.substr(1, v.size() - 2);
        v = trim(v);
        if (!v.empty()) out.push_back(v);
        cur.clear();
    };
    for (char c : body) { if (c == ',') flush(); else cur += c; }
    flush();
    return out;
}

std::vector<Scanner> resolve_scanners(const std::vector<Scanner>& declared,
                                      const ScannerProbe& probe) {
    std::vector<Scanner> out = declared;
    for (auto& s : out) {
        // PATH availability: use the declared bin, else the scanner name.
        std::string tool = s.bin.empty() ? s.name : s.bin;
        s.on_path = probe.tool_on_path ? probe.tool_on_path(tool) : false;

        for (const auto& rel : known_config_files(s.name)) {
            std::string full = probe.repo_root.empty()
                                   ? rel
                                   : platform::path_join(probe.repo_root, rel);
            bool present = probe.exists ? probe.exists(full) : false;
            s.config_files.push_back({rel, present});
        }
    }
    return out;
}

}  // namespace keur::config
