/**
 * @file config/disabled.cpp
 * @brief Pure disable-list logic: parse [rules] disabled, union across layers,
 *        render back. No I/O of its own (read_file is injected).
 *        @implements KEUR-DISABLED
 */
#include "config/disabled.h"

namespace keur::config {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Pull every quoted token out of a chunk of text into the id set. Used for the
// `disabled = [...]` array whether it is inline or spread over lines.
void collect_quoted(const std::string& chunk, std::set<std::string>& out) {
    bool in = false;
    char q = 0;
    std::string cur;
    for (char c : chunk) {
        if (!in && (c == '"' || c == '\'')) { in = true; q = c; cur.clear(); }
        else if (in && c == q) { in = false; if (!cur.empty()) out.insert(cur); }
        else if (in) cur += c;
    }
}

}  // namespace

std::set<std::string> parse_disabled(const std::string& toml_body) {
    std::set<std::string> ids;
    bool in_rules = false;
    bool in_array = false;
    std::string line;
    size_t pos = 0;

    auto next = [&](std::string& out) -> bool {
        if (pos >= toml_body.size()) return false;
        size_t nl = toml_body.find('\n', pos);
        if (nl == std::string::npos) { out = toml_body.substr(pos); pos = toml_body.size(); }
        else { out = toml_body.substr(pos, nl - pos); pos = nl + 1; }
        return true;
    };

    while (next(line)) {
        std::string t = trim(line);
        if (!in_array) {
            if (t.empty() || t[0] == '#') continue;
            if (t.front() == '[') { in_rules = (t == "[rules]"); continue; }
        }
        if (!in_rules) continue;

        if (in_array) {
            collect_quoted(t, ids);
            if (t.find(']') != std::string::npos) in_array = false;
            continue;
        }

        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        if (trim(t.substr(0, eq)) != "disabled") continue;
        std::string rhs = t.substr(eq + 1);
        collect_quoted(rhs, ids);
        // multi-line array: opened with [ but no closing ] on this line.
        if (rhs.find('[') != std::string::npos && rhs.find(']') == std::string::npos)
            in_array = true;
    }
    return ids;
}

std::set<std::string> resolve_disabled(
    const std::vector<Layer>& layers,
    const std::function<std::string(const std::string&)>& read_file) {
    std::set<std::string> all;
    for (const auto& l : layers) {
        if (l.path.empty() || !l.exists) continue;
        std::string body = read_file(l.path);
        if (body.empty()) continue;
        for (const auto& id : parse_disabled(body)) all.insert(id);
    }
    return all;
}

std::string render_disabled_section(const std::set<std::string>& ids) {
    std::string s = "[rules]\ndisabled = [";
    bool first = true;
    for (const auto& id : ids) {  // std::set iterates sorted → stable diff
        if (!first) s += ", ";
        s += "\"" + id + "\"";
        first = false;
    }
    s += "]\n";
    return s;
}

std::string upsert_disabled_section(const std::string& toml_body,
                                    const std::set<std::string>& ids) {
    // Split into lines (keep it simple + line-oriented so unrelated sections
    // are preserved byte-for-byte). Our writer always emits an INLINE disabled
    // array, so replacing the single `disabled = [...]` line is enough for a
    // round-trip of config keur wrote itself.
    std::vector<std::string> lines;
    { std::string cur; for (char c : toml_body) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) lines.push_back(cur); }

    const std::string disabled_line =
        ids.empty() ? "" : ("disabled = " + [&]{
            std::string a = "[";
            bool first = true;
            for (const auto& id : ids) { if (!first) a += ", "; a += "\"" + id + "\""; first = false; }
            a += "]";
            return a;
        }());

    std::vector<std::string> out;
    bool in_rules = false, wrote_disabled = false, saw_rules = false;
    for (const auto& raw : lines) {
        std::string t = trim(raw);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            // leaving [rules]: if we never wrote the disabled line, add it now.
            if (in_rules && !wrote_disabled && !disabled_line.empty()) {
                out.push_back(disabled_line); wrote_disabled = true;
            }
            in_rules = (t == "[rules]");
            if (in_rules) saw_rules = true;
            out.push_back(raw);
            continue;
        }
        if (in_rules && t.rfind("disabled", 0) == 0 && t.find('=') != std::string::npos) {
            // replace the existing disabled line (or drop it when ids empty).
            if (!disabled_line.empty()) out.push_back(disabled_line);
            wrote_disabled = true;
            continue;
        }
        out.push_back(raw);
    }
    // [rules] existed but disabled line was never emitted (e.g. section at EOF).
    if (in_rules && !wrote_disabled && !disabled_line.empty()) {
        out.push_back(disabled_line); wrote_disabled = true;
    }

    std::string result;
    for (const auto& l : out) { result += l; result += "\n"; }

    // No [rules] section at all → append one (unless there is nothing to write).
    if (!saw_rules && !disabled_line.empty()) {
        if (!result.empty() && result.back() != '\n') result += "\n";
        result += "\n[rules]\n" + disabled_line + "\n";
    }
    return result;
}

}  // namespace keur::config
