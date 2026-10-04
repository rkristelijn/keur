/**
 * @file manager/view.cpp
 * @brief Pure rendering of the manager tree. No I/O.
 *        @implements KEUR-MANAGER-VIEW
 */
#include "manager/view.h"

#include <algorithm>

#include "render/display_width.h"

namespace keur::manager {

namespace {

// SGR colour helpers — only emitted when opts.color is true.
constexpr const char* kReset   = "\033[0m";
constexpr const char* kDim     = "\033[2m";
constexpr const char* kGreen   = "\033[32m";
constexpr const char* kReverse = "\033[7m";   // focused row highlight
// Scope-header colours, keyed to reach/blast-radius so a screenshot reads at a
// glance: system-wide vs. this-repo vs. the read-only baseline.
constexpr const char* kCyan    = "\033[36m";  // Global — system-wide (git --global)
constexpr const char* kYellow  = "\033[33m";  // This repo — repo-level, here & now
constexpr const char* kRed     = "\033[31m";  // warning (shadowed / not chained)

std::string indent_for(int depth) {
    // 2 spaces per level; the glyph column follows.
    return std::string(static_cast<std::size_t>(depth) * 2, ' ');
}

// Rule glyph: ✓ on, ☐ off (todo). Read-only catalogue rules use •/◦.
std::string glyph(const Node& n) {
    if (n.kind == NodeKind::SettingNode) return " ";
    if (n.kind == NodeKind::RuleNode || n.kind == NodeKind::ScannerNode) {
        if (n.read_only) return n.enabled ? "•" : "◦";  // • / ◦
        return n.enabled ? "✓" : "☐";  // ✓ (done) / ☐ (todo)
    }
    return n.expanded ? "▼" : "▶";  // ▼ / ▶ (expand state)
}

// Editable-group checkbox reflecting how many of its rules are on:
//   [x] all on   [ ] all off   [~] some on (partial)
// Read-only groups (Defaults) get no checkbox (spacing preserved for alignment).
std::string group_checkbox(const Node& n) {
    // Only group nodes get a group-checkbox; leaves carry their own via glyph().
    if (n.kind == NodeKind::RuleNode || n.kind == NodeKind::SettingNode ||
        n.kind == NodeKind::ScannerNode) return "";
    if (n.read_only) return "";
    GroupCount gc = group_count(n);
    if (gc.total == 0) return "";
    if (gc.enabled == 0)        return " ☐";  // ☐ all off
    if (gc.enabled == gc.total) return " ✓";  // ✓ all on
    return " ◦";  // ◦ partial (some on)
}

}  // namespace

std::string render_row(const Row& row) {
    const Node& n = *row.node;
    // For an editable group, put a checkbox (✓/☐/◦) between the expand arrow
    // and the label so you see at a glance whether the whole group is on.
    std::string s = indent_for(n.depth) + glyph(n) + group_checkbox(n) + " " + n.label;
    // A group flagged with a problem (e.g. Hooks shadowed) shows the ⚠ marker
    // after its expand arrow so the title itself signals the issue.
    if (n.kind == NodeKind::CategoryNode && n.warn)
        s = indent_for(n.depth) + glyph(n) + " ⚠ " + n.label;
    if (n.kind == NodeKind::ActionNode) {
        // An interactive action: ☐ + label (Space runs it). Shown at leaf depth.
        s = indent_for(n.depth) + "☐ " + n.label;
    } else if (n.kind == NodeKind::SettingNode) {
        if (n.warn) s = indent_for(n.depth) + "⚠ " + n.label;  // ⚠ prefix for problems
        if (!n.detail.empty()) s += " = " + n.detail;
    } else if (n.kind == NodeKind::ScannerNode) {
        if (!n.detail.empty()) s += "  " + n.detail;                 // group (secrets/sast/…)
        if (n.on_path)          s += "   on PATH ✓";            // installed ✓
        else if (!n.hint.empty()) s += "   NOT installed — " + n.hint;  // install hint
        else                     s += "   NOT installed";
        if (n.inherited)         s += "   (overruled by repo)";
        else if (!n.enabled)     s += "   (disabled)";
    } else if (n.kind == NodeKind::RuleNode) {
        if (!n.detail.empty()) s += "  " + n.detail;
        if (n.deprecated)      s += "   (deprecated)";
        if (n.inherited)       s += "   (overruled by repo)";
        else if (!n.enabled)   s += "   (disabled)";
    } else if (!n.read_only) {
        // Editable group: also show an [enabled/total] count badge.
        GroupCount gc = group_count(n);
        if (gc.total > 0) {
            s += "  [" + std::to_string(gc.enabled) + "/" +  // keur:ignore KEUR-PORT-004 (display badge, not a path)
                 std::to_string(gc.total) + "]";
        }
    }
    return s;
}

// True when an editable group has all its rules disabled (→ render greyed).
static bool group_all_off(const Node& n) {
    if (n.kind == NodeKind::RuleNode || n.read_only) return false;
    GroupCount gc = group_count(n);
    return gc.total > 0 && gc.enabled == 0;
}

// True when an editable group's rule leaves are ALL overruled by a more
// specific scope (every leaf `inherited`). Such a group does nothing at this
// scope, so it is greyed like its leaves. A PARTIALLY overruled group is left
// un-greyed on purpose: dimming it would hide that rules are still live under
// it; the [enabled/total] badge communicates the partial state instead.
// Scanners are excluded (they do not carry the repo-override cascade here).
// @id KEUR-MANAGER-VIEW.group-overruled
static bool group_all_inherited(const Node& n) {
    if (n.kind == NodeKind::RuleNode || n.kind == NodeKind::ScannerNode || n.read_only)
        return false;
    int total = 0, inherited = 0;
    struct W {
        int* total; int* inherited;
        void walk(const Node& x) {
            if (x.kind == NodeKind::RuleNode) {
                if (!x.read_only) { ++*total; if (x.inherited) ++*inherited; }
                return;
            }
            if (x.kind == NodeKind::ScannerNode) return;  // not part of this cascade
            for (const auto& c : x.children) walk(c);
        }
    };
    W w{&total, &inherited};
    w.walk(n);
    return total > 0 && inherited == total;
}

std::vector<std::string> render(const Model& m, const ViewOpts& opts) {
    std::vector<std::string> out;
    auto rows = visible_rows(m);
    const int width = std::max(20, opts.cols);

    // Header (3 lines): title + tagline + separator. The tagline makes a
    // screenshot self-explanatory — you see what keur manager does at a glance.
    std::string title = "keur manager";
    if (!opts.version.empty()) title += " · v" + opts.version;  // keur manager · vX.Y.Z
    std::string hdr = title;
    if (m.dirty) hdr += "  • unsaved changes";
    out.push_back(render::truncate_to(hdr, width));
    {
        // Tagline + current grouping order, so a screenshot shows both what the
        // tool does and how the tree is grouped right now.
        std::string grouping;
        for (std::size_t i = 0; i < m.order.size(); ++i) {
            if (i) grouping += " › ";
            grouping += facet_name(m.order[i]);
        }
        std::string line = "manage your git hooks & rules in one place";
        if (!grouping.empty()) line += "   ·   grouped by: " + grouping + "  (o to reorder)";
        std::string tagline = render::truncate_to(line, width);
        if (opts.color) tagline = std::string(kDim) + tagline + kReset;
        out.push_back(tagline);
    }
    out.push_back(std::string(static_cast<std::size_t>(std::min(width, 60)), '-'));

    // Body height = total rows minus header(3) and footer(1).
    int body_h = std::max(1, opts.rows - 4);
    const std::size_t row_count = rows.size();
    int total  = static_cast<int>(row_count);
    int cursor = std::clamp(m.cursor, 0, std::max(0, total - 1));

    // Scroll so the cursor stays visible (simple top-anchored window).
    int start = 0;
    if (cursor >= body_h) start = cursor - body_h + 1;
    int end = std::min(total, start + body_h);

    for (int i = start; i < end; ++i) {
        const Node& n = *rows[static_cast<std::size_t>(i)].node;
        std::string text = render::truncate_to(render_row(rows[static_cast<std::size_t>(i)]), width);
        std::string line;
        const bool focused_row = (i == cursor);

        if (!opts.color) {
            line = (focused_row ? "❯ " : "  ") + text;  // ❯ marker, no SGR
        } else {
            std::string body = text;
            if (n.kind == NodeKind::ScopeNode) {
                // Scope headers coloured by reach: Global=cyan (system-wide),
                // This repo=blue (repo-level), Defaults=dim (read-only baseline).
                const char* sc = (n.scope == Scope::Global) ? kCyan
                               : (n.scope == Scope::Repo)   ? kYellow
                                                            : kDim;
                body = std::string(sc) + text + kReset;
            }
            else if (n.warn)
                body = std::string(kRed) + text + kReset;   // shadowed / not chained: red ⚠
            else if (n.read_only || n.kind == NodeKind::SettingNode || n.inherited)
                body = std::string(kDim) + text + kReset;   // baseline / cascaded: dimmed
            else if ((n.kind == NodeKind::RuleNode || n.kind == NodeKind::ScannerNode) && n.enabled)
                body = std::string(kGreen) + text + kReset;
            else if ((n.kind == NodeKind::RuleNode || n.kind == NodeKind::ScannerNode) && !n.enabled)
                body = std::string(kDim) + text + kReset;
            else if (group_all_off(n) || group_all_inherited(n))
                body = std::string(kDim) + text + kReset;   // whole group off / fully overruled: greyed
            if (focused_row)
                line = std::string(kReverse) + "❯ " + body + kReset;
            else
                line = "  " + body;
        }
        out.push_back(line);
    }

    // Pad body to full height so the frame doesn't jump as it scrolls.
    for (int i = end - start; i < body_h; ++i) out.push_back("");

    // Footer: keybinding hint.
    std::string foot = "↑↓/jk move  ←→/hl fold  "
                       "Space toggle  o reorder  s save  q quit  ? help";
    if (opts.color) foot = std::string(kDim) + foot + kReset;
    out.push_back(foot);

    return out;
}

}  // namespace keur::manager
