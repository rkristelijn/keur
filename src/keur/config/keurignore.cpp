/**
 * @file config/keurignore.cpp
 * @implements ADR-035
 */
#include "config/keurignore.h"

#include <cctype>

namespace keur::config {

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool looks_like_rule_id(const std::string& t) {
    if (t.empty() || t.find('/') != std::string::npos) return false;
    bool has_alpha = false;
    for (char c : t) {
        if (c >= 'a' && c <= 'z') return false;
        if (c >= 'A' && c <= 'Z') has_alpha = true;
        else if (!((c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return has_alpha;
}

bool is_all_digits(const std::string& t) {
    if (t.empty()) return false;
    for (char c : t) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool is_secret_category(const std::string& cat) {
    return cat == "secret" || cat == "secrets";
}
}  // namespace

std::vector<IgnoreEntry> parse_keurignore(const std::string& body) {
    std::vector<IgnoreEntry> out;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t nl = body.find('\n', pos);
        std::string line = body.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? body.size() + 1 : nl + 1;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        // "secret <value|sha256:hash>" — value-based, the ONLY form that
        // suppresses a secret finding (a moved secret stays hidden; a changed
        // one is caught again).
        if (line.rfind("secret ", 0) == 0) {
            IgnoreEntry e;
            e.secret_value = trim(line.substr(7));
            if (!e.secret_value.empty()) out.push_back(e);
            continue;
        }

        // Location forms: split on ':' into up to path:line:rule (from the right).
        // Recognise a trailing RULE-ID and/or a trailing LINE number.
        IgnoreEntry e;
        std::string rest = line;
        // trailing rule id?
        size_t c1 = rest.rfind(':');
        if (c1 != std::string::npos && looks_like_rule_id(trim(rest.substr(c1 + 1)))) {
            e.rule = trim(rest.substr(c1 + 1));
            rest = trim(rest.substr(0, c1));
        }
        // trailing line number?
        size_t c2 = rest.rfind(':');
        if (c2 != std::string::npos && is_all_digits(trim(rest.substr(c2 + 1)))) {
            e.line = std::stoi(trim(rest.substr(c2 + 1)));
            rest = trim(rest.substr(0, c2));
        }
        // whatever remains is either a bare rule id or a path
        if (e.rule.empty() && e.line == 0 && looks_like_rule_id(rest)) {
            e.rule = rest;  // bare RULE-ID -> suppress that (non-secret) rule everywhere
        } else if (!rest.empty()) {
            e.path = rest;
        }
        if (!e.rule.empty() || !e.path.empty() || e.line != 0) out.push_back(e);
    }
    return out;
}

bool keurignore_suppresses(const std::vector<IgnoreEntry>& entries,
                           const std::string& category, const std::string& file,
                           int line, const std::string& rule,
                           const std::string& matched) {
    const bool secret = is_secret_category(category);
    for (const auto& e : entries) {
        if (!e.secret_value.empty()) {
            // Secret-value entry: only ever suppresses a SECRET finding, and only
            // when the matched value equals the entry (literal; sha256: reserved
            // for when findings expose a hash — see ADR-035 follow-up).
            if (secret && !matched.empty() && e.secret_value == matched) return true;
            continue;  // never suppresses non-secret findings
        }
        // Location/rule entry: NEVER suppresses a secret (the maintainer's rule —
        // a line-suppress must not silence a secret; keur keeps checking it).
        if (secret) continue;
        bool path_ok = e.path.empty() || (!file.empty() && file.find(e.path) != std::string::npos);
        bool line_ok = e.line == 0 || e.line == line;
        bool rule_ok = e.rule.empty() || e.rule == rule;
        if (path_ok && line_ok && rule_ok) return true;
    }
    return false;
}

std::vector<IgnoreEntry> resolve_keurignore(
    const std::string& repo_root,
    const std::function<std::string(const std::string&)>& read_file) {
    std::string base = repo_root.empty() ? "." : repo_root;
    for (const std::string& rel : {"/.keurignore", "/.config/.keurignore", "/.config/keurignore"}) {
        std::string body = read_file(base + rel);
        if (!body.empty()) return parse_keurignore(body);
    }
    return {};
}

}  // namespace keur::config
