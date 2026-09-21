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
