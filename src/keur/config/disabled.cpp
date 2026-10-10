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

    auto next = [&pos, &toml_body](std::string& out) -> bool {
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

namespace {

// Normalise a path for prefix comparison: trim, strip a trailing '/'. Keeps the
// comparison a plain string op (ADR-043 keeps path matching pure — no fs).
std::string norm_path(std::string p) {
    p = trim(p);
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

}  // namespace

bool path_matches_prefix(const std::string& glob, const std::string& repo_root,
                         const std::string& home) {
    std::string g = trim(glob);
    if (g.empty()) return false;

    // Expand a leading "~/" (or bare "~") with HOME.
    if (!home.empty() && (g == "~" || g.rfind("~/", 0) == 0))
        g = home + g.substr(1);

    // A trailing "/**" means "this dir and everything under it". Strip it to a
    // plain prefix; the match below already covers "root itself or beneath".
    bool had_globstar = false;
    if (g.size() >= 3 && g.compare(g.size() - 3, 3, "/**") == 0) {
        g = g.substr(0, g.size() - 3);
        had_globstar = true;
    }
    (void)had_globstar;  // same semantics either way; kept for intent/readability

    std::string prefix = norm_path(g);
    std::string root = norm_path(repo_root);
    if (prefix.empty() || root.empty()) return false;

    if (root == prefix) return true;                 // the dir itself
    // Strictly beneath the prefix. The '/' here is a LOGICAL path separator on
    // git-normalised roots (git rev-parse emits forward slashes on every OS)
    // and on scope globs the user writes with '/', not a filesystem path we
    // build — so platform::path_join does not apply (ADR-005). Deliberate.
    if (root.rfind(prefix + "/", 0) == 0) return true;  // keur:ignore KEUR-PORT-004
    return false;
}

namespace {

// Parse a section header body (the text between [ and ]) that begins with
// `scopes.`. Returns the glob (from the quoted key) and the sub-table suffix.
// `[scopes."~/git/lab/**".rules]` → glob=`~/git/lab/**`, suffix=`rules`.
// Returns false if the header is not a scopes.* section or is malformed.
bool parse_scope_header(const std::string& inner, std::string& glob,
                        std::string& suffix) {
    const std::string pfx = "scopes.";
    if (inner.rfind(pfx, 0) != 0) return false;
    size_t i = pfx.size();
    if (i >= inner.size() || (inner[i] != '"' && inner[i] != '\'')) return false;
    char q = inner[i++];
    size_t end = inner.find(q, i);
    if (end == std::string::npos) return false;
    glob = inner.substr(i, end - i);
    // Remainder after the closing quote: optional ".<suffix>" (e.g. ".rules").
    std::string rest = inner.substr(end + 1);
    suffix = (!rest.empty() && rest.front() == '.') ? rest.substr(1) : "";
    return true;
}

}  // namespace

std::set<std::string> scoped_disabled(const std::string& toml_body,
                                      const std::string& repo_root,
                                      const std::string& home) {
    std::set<std::string> ids;
    if (repo_root.empty()) return ids;

    // Walk sections. We only care about `[scopes."<glob>".rules]` sub-tables
    // whose glob matches the repo root; inside them we collect `disabled`
    // entries (inline or multi-line array), exactly like parse_disabled does
    // for [rules]. Any non-scopes section ends the current scope context.
    std::string line;
    size_t pos = 0;
    auto next = [&pos, &toml_body](std::string& out) -> bool {
        if (pos >= toml_body.size()) return false;
        size_t nl = toml_body.find('\n', pos);
        if (nl == std::string::npos) { out = toml_body.substr(pos); pos = toml_body.size(); }
        else { out = toml_body.substr(pos, nl - pos); pos = nl + 1; }
        return true;
    };

    bool collecting = false;  // inside a matching [scopes."<glob>".rules]
    bool in_array = false;    // inside a multi-line disabled = [ ... ]

    while (next(line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (!in_array && t.front() == '[' && t.back() == ']') {
            std::string inner = trim(t.substr(1, t.size() - 2));
            std::string glob, suffix;
            if (parse_scope_header(inner, glob, suffix)) {
                // A scopes.* section: collect only its `.rules` sub-table, and
                // only when the glob matches this repo root.
                collecting = (suffix == "rules") &&
                             path_matches_prefix(glob, repo_root, home);
            } else {
                collecting = false;  // a non-scopes section ends scope context
            }
            continue;
        }

        if (!collecting) continue;

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
        if (rhs.find('[') != std::string::npos && rhs.find(']') == std::string::npos)
            in_array = true;
    }
    return ids;
}

std::set<std::string> resolve_disabled(
    const std::vector<Layer>& layers,
    const std::function<std::string(const std::string&)>& read_file,
    const std::string& repo_root,
    const std::string& home) {
    std::set<std::string> all;
    for (const auto& l : layers) {
        if (l.path.empty() || !l.exists) continue;
        std::string body = read_file(l.path);
        if (body.empty()) continue;
        // Flat [rules] disabled — applies everywhere this layer applies.
        for (const auto& id : parse_disabled(body)) all.insert(id);
        // Path-scoped [scopes."<glob>".rules] disabled — applies only when the
        // repo root matches the glob. Lives in the SAME keur.toml (ADR-043), so
        // it is read from every layer, not a separate file. When not in a repo
        // there is nothing to match, so scoped sections contribute nothing.
        if (!repo_root.empty())
            for (const auto& id : scoped_disabled(body, repo_root, home)) all.insert(id);
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
        ids.empty() ? "" : ("disabled = " + [&ids]{
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
