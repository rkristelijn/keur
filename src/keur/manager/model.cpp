/**
 * @file manager/model.cpp
 * @brief Pure tree model + reducers for the manager TUI. No I/O.
 *        @implements KEUR-MANAGER-MODEL
 */
#include "manager/model.h"

#include <algorithm>
#include <map>

namespace keur::manager {

std::vector<Facet> default_facet_order() {
    return {Facet::Category, Facet::Subcategory, Facet::Language, Facet::Framework};
}

std::vector<Facet> next_facet_order(const std::vector<Facet>& current) {
    // Two useful presets: category-first (default) and language-first. Cycle
    // between them so one key flips the whole grouping.
    static const std::vector<Facet> category_first =
        {Facet::Category, Facet::Subcategory, Facet::Language, Facet::Framework};
    static const std::vector<Facet> language_first =
        {Facet::Language, Facet::Framework, Facet::Category, Facet::Subcategory};
    return (current == category_first) ? language_first : category_first;
}

std::string facet_name(Facet f) {
    switch (f) {
        case Facet::Category:    return "category";
        case Facet::Subcategory: return "subcategory";
        case Facet::Language:    return "language";
        case Facet::Framework:   return "framework";
    }
    return "?";
}

std::string facet_value(const RuleItem& item, Facet f) {
    switch (f) {
        case Facet::Category:    return item.category;
        case Facet::Subcategory: return item.subcategory;
        case Facet::Language:    return item.language;
        case Facet::Framework:   return item.framework;
    }
    return "";
}

std::string scope_name(Scope s) {
    switch (s) {
        case Scope::Defaults: return "Defaults";
        case Scope::Global:   return "Global";
        case Scope::Repo:     return "This repo";
    }
    return "?";
}

namespace {

// Make a rule leaf node from an item (shared by all groupers).
Node make_leaf(const RuleItem& item, Scope scope, bool read_only, int depth) {
    Node leaf;
    leaf.kind = NodeKind::RuleNode;
    leaf.label = item.id;
    leaf.scope = scope;
    leaf.rule_id = item.id;
    leaf.detail = item.detail;
    leaf.enabled = item.enabled;
    leaf.baseline = item.enabled;
    leaf.read_only = read_only;
    leaf.deprecated = item.deprecated;
    leaf.inherited = item.inherited;
    leaf.depth = depth;
    return leaf;
}

// Group a set of rule items into a variable-depth subtree by the facet `order`.
// At each level, items are bucketed by that facet's value (first-seen order for
// stability); a bucket whose value is "any" is SKIPPED (its items flow to the
// next facet directly) so agnostic rules don't create noise levels. When the
// facets run out, the remaining items become rule leaves. Pure + recursive.
std::vector<Node> group_by_facets(const std::vector<RuleItem>& items, Scope scope,
                                  bool read_only, const std::vector<Facet>& order,
                                  std::size_t level, int depth) {
    // No more facets to group by → emit rule leaves (sorted by id already).
    if (level >= order.size()) {
        std::vector<Node> leaves;
        for (const auto& it : items) leaves.push_back(make_leaf(it, scope, read_only, depth));
        return leaves;
    }

    Facet f = order[level];
    // Partition items by this facet's value, preserving first-seen order. Items
    // whose value is "any" for this facet skip the level (grouped one deeper).
    std::vector<std::string> keys;                 // ordered distinct values
    std::map<std::string, std::vector<RuleItem>> buckets;
    std::vector<RuleItem> passthrough;             // value == "any"
    for (const auto& it : items) {
        std::string v = facet_value(it, f);
        if (v.empty() || v == "any") { passthrough.push_back(it); continue; }
        if (!buckets.count(v)) keys.push_back(v);
        buckets[v].push_back(it);
    }

    std::vector<Node> out;
    for (const auto& key : keys) {
        Node grp;
        grp.kind = NodeKind::CategoryNode;
        grp.label = key;
        grp.scope = scope;
        grp.depth = depth;
        grp.expanded = false;              // catalogues are large: start collapsed
        grp.read_only = read_only;
        grp.children = group_by_facets(buckets[key], scope, read_only, order,
                                       level + 1, depth + 1);
        out.push_back(std::move(grp));
    }
    // Passthrough ("any") items are grouped by the REMAINING facets at THIS
    // depth (no extra level for an agnostic facet), then appended.
    if (!passthrough.empty()) {
        auto rest = group_by_facets(passthrough, scope, read_only, order,
                                    level + 1, depth);
        for (auto& n : rest) out.push_back(std::move(n));
    }
    return out;
}

// Build a "Scanners" group of toggleable external-tool leaves for a scope.
Node build_scanner_group(Scope scope, const std::vector<ScannerItem>& scanners,
                         bool read_only, int depth) {
    Node grp;
    grp.kind = NodeKind::CategoryNode;
    grp.label = "Scanners (external tools)";
    grp.scope = scope;
    grp.depth = depth;
    grp.expanded = false;
    grp.read_only = read_only;
    for (const auto& sc : scanners) {
        Node leaf;
        leaf.kind = NodeKind::ScannerNode;
        leaf.label = sc.name;
        leaf.scope = scope;
        leaf.rule_id = sc.name;   // reuse rule_id as the toggle target id
        leaf.detail = sc.group;
        leaf.enabled = sc.enabled;
        leaf.baseline = sc.enabled;
        leaf.read_only = read_only;
        leaf.inherited = sc.inherited;
        leaf.on_path = sc.on_path;
        leaf.hint = sc.install_hint;
        leaf.depth = depth + 1;
        grp.children.push_back(std::move(leaf));
    }
    return grp;
}

Node build_scope(Scope scope, const std::vector<RuleItem>& rules, bool read_only,
                 const std::string& path, const std::vector<Facet>& order,
                 const std::vector<ScannerItem>& scanners = {},
                 const std::vector<HookInfoItem>& hooks = {}) {
    Node s;
    s.kind = NodeKind::ScopeNode;
    s.label = scope_name(scope);
    if (!path.empty()) s.label += "  (" + path + ")";
    else if (scope != Scope::Defaults) s.label += "  (no keur.toml yet)";
    s.scope = scope;
    s.depth = 0;
    s.expanded = true;
    s.read_only = read_only;
    // Hooks info first (read-only): which manager owns hooks here + keur status.
    if (!hooks.empty()) {
        bool any_warn = false;
        for (const auto& h : hooks) if (h.warn) { any_warn = true; break; }
        Node grp;
        grp.kind = NodeKind::CategoryNode;
        grp.label = "Hooks";
        grp.scope = scope;
        grp.depth = 1;
        grp.expanded = any_warn;   // auto-open when there's a problem to see
        grp.read_only = true;      // informational; toggling comes later
        grp.warn = any_warn;       // red ⚠ on the title when shadowed/not chained
        for (const auto& h : hooks) {
            Node leaf;
            leaf.kind = h.action.empty() ? NodeKind::SettingNode : NodeKind::ActionNode;
            leaf.label = h.label;
            leaf.scope = scope;
            leaf.detail = h.value;
            leaf.read_only = h.action.empty();   // action leaves are interactive
            leaf.warn = h.warn;
            leaf.action = h.action;
            leaf.depth = 2;
            grp.children.push_back(std::move(leaf));
        }
        s.children.push_back(std::move(grp));
    }
    // Scanners next (a short, high-value list), then the facet-grouped rules.
    if (!scanners.empty())
        s.children.push_back(build_scanner_group(scope, scanners, read_only, /*depth=*/1));
    auto rule_groups = group_by_facets(rules, scope, read_only, order, /*level=*/0, /*depth=*/1);
    for (auto& g : rule_groups) s.children.push_back(std::move(g));
    return s;
}

// Build the read-only Defaults scope: the shipped template's settings grouped
// under labelled sub-groups, then the full rule catalogue as the baseline.
Node build_defaults_scope(const DefaultsInfo& d, const std::vector<Facet>& order) {
    Node s;
    s.kind = NodeKind::ScopeNode;
    s.label = scope_name(Scope::Defaults);
    s.scope = Scope::Defaults;
    s.depth = 0;
    s.expanded = true;
    s.read_only = true;

    // Settings sub-groups (Scanners, Policy, …), each a read-only category.
    for (const auto& si : d.settings) {
        Node* grp = nullptr;
        for (auto& c : s.children)
            if (c.label == si.group) { grp = &c; break; }
        if (!grp) {
            Node g;
            g.kind = NodeKind::CategoryNode;
            g.label = si.group;
            g.scope = Scope::Defaults;
            g.depth = 1;
            g.expanded = false;
            g.read_only = true;
            s.children.push_back(std::move(g));
            grp = &s.children.back();
        }
        Node leaf;
        leaf.kind = NodeKind::SettingNode;
        leaf.label = si.label;
        leaf.scope = Scope::Defaults;
        leaf.detail = si.value;
        leaf.read_only = true;
        leaf.depth = 2;
        grp->children.push_back(std::move(leaf));
    }

    // The full rule catalogue as a "Rules" sub-tree (baseline, all read-only),
    // grouped by the same facet order. Nested one level under the group.
    Node rules_group;
    rules_group.kind = NodeKind::CategoryNode;
    rules_group.label = "Rules (catalogue)";
    rules_group.scope = Scope::Defaults;
    rules_group.depth = 1;
    rules_group.expanded = false;
    rules_group.read_only = true;
    rules_group.children = group_by_facets(d.catalog, Scope::Defaults, /*read_only=*/true,
                                           order, /*level=*/0, /*depth=*/2);
    s.children.push_back(std::move(rules_group));
    return s;
}

// Depth-first collection of visible rows, honouring `expanded`.
void collect_visible(const Node& n, std::vector<Row>& out) {
    out.push_back({&n});
    if ((n.kind != NodeKind::RuleNode && n.kind != NodeKind::ScannerNode && n.kind != NodeKind::ActionNode) && n.expanded)
        for (const auto& c : n.children) collect_visible(c, out);
}

// The visible index of the parent of the row at `idx`: the nearest preceding
// row with a smaller depth (-1 for a top-level scope node).
int parent_index(const std::vector<Row>& rows, int idx) {
    if (idx <= 0) return -1;
    int d = rows[static_cast<std::size_t>(idx)].node->depth;
    for (int i = idx - 1; i >= 0; --i)
        if (rows[static_cast<std::size_t>(i)].node->depth < d) return i;
    return -1;
}

// Mutable lookup of the node at a visible index (mirrors collect_visible).
struct Walker {
    int target;
    int counter = 0;
    Node* find(Node& n) {
        if (counter == target) return &n;
        ++counter;
        if ((n.kind != NodeKind::RuleNode && n.kind != NodeKind::ScannerNode && n.kind != NodeKind::ActionNode) && n.expanded)
            for (auto& c : n.children)
                if (Node* r = find(c)) return r;
        return nullptr;
    }
};

Node* visible_node_mut(Model& m, int idx) {
    Walker w{idx};
    for (auto& root : m.roots)
        if (Node* r = w.find(root)) return r;
    return nullptr;
}

}  // namespace

Model build_model(const DefaultsInfo& defaults,
                  const std::vector<RuleItem>& global_rules,
                  const std::vector<RuleItem>& repo_rules,
                  bool repo_available,
                  const std::string& global_path,
                  const std::string& repo_path,
                  const std::vector<Facet>& order,
                  const std::vector<ScannerItem>& global_scanners,
                  const std::vector<ScannerItem>& repo_scanners,
                  const std::vector<HookInfoItem>& global_hooks,
                  const std::vector<HookInfoItem>& repo_hooks) {
    Model m;
    m.repo_available = repo_available;
    m.order = order;
    m.roots.push_back(build_defaults_scope(defaults, order));
    m.roots.push_back(build_scope(Scope::Global, global_rules, /*read_only=*/false,
                                  global_path, order, global_scanners, global_hooks));
    if (repo_available)
        m.roots.push_back(build_scope(Scope::Repo, repo_rules, /*read_only=*/false,
                                      repo_path, order, repo_scanners, repo_hooks));
    m.cursor = 0;
    return m;
}

std::vector<Row> visible_rows(const Model& m) {
    std::vector<Row> out;
    for (const auto& root : m.roots) collect_visible(root, out);
    return out;
}

const Node* focused(const Model& m) {
    auto rows = visible_rows(m);
    if (rows.empty()) return nullptr;
    const std::size_t count = rows.size();
    int c = std::clamp(m.cursor, 0, static_cast<int>(count) - 1);
    return rows[static_cast<std::size_t>(c)].node;
}

std::string focused_action(const Model& m) {
    const Node* n = focused(m);
    return (n && n->kind == NodeKind::ActionNode) ? n->action : std::string();
}

void move_cursor(Model& m, int delta) {
    int n = static_cast<int>(visible_rows(m).size());
    if (n == 0) { m.cursor = 0; return; }
    m.cursor = std::clamp(m.cursor + delta, 0, n - 1);
}

void cursor_top(Model& m) { m.cursor = 0; }

void cursor_bottom(Model& m) {
    int n = static_cast<int>(visible_rows(m).size());
    m.cursor = n > 0 ? n - 1 : 0;
}

bool expand_focused(Model& m) {
    Node* n = visible_node_mut(m, m.cursor);
    if (!n || n->kind == NodeKind::RuleNode || n->expanded) return false;
    n->expanded = true;
    return true;
}

bool collapse_focused(Model& m) {
    Node* n = visible_node_mut(m, m.cursor);
    if (!n) return false;
    if ((n->kind != NodeKind::RuleNode && n->kind != NodeKind::ScannerNode && n->kind != NodeKind::ActionNode) && n->expanded) {
        n->expanded = false;
        return true;
    }
    // Already collapsed, or a leaf: jump to parent (file-tree convention).
    auto rows = visible_rows(m);
    int p = parent_index(rows, m.cursor);
    if (p >= 0) { m.cursor = p; return true; }
    return false;
}

std::vector<Edit> pending_edits(const Model& m) {
    std::vector<Edit> edits;
    struct EWalker {
        std::vector<Edit>* out;
        void walk(const Node& n) {
            if (n.kind == NodeKind::RuleNode) {
                if (n.enabled != n.baseline)
                    out->push_back({n.scope, n.rule_id, /*disable=*/!n.enabled, /*is_scanner=*/false});
            } else if (n.kind == NodeKind::ScannerNode) {
                if (n.enabled != n.baseline)
                    out->push_back({n.scope, n.rule_id, /*disable=*/!n.enabled, /*is_scanner=*/true});
            } else {
                for (const auto& c : n.children) walk(c);
            }
        }
    };
    EWalker w{&edits};
    for (const auto& root : m.roots) w.walk(root);
    return edits;
}

bool toggle_focused_rule(Model& m) {
    Node* n = visible_node_mut(m, m.cursor);
    if (!n) return false;
    // Toggle a rule OR a scanner leaf (both are on/off items).
    if (n->kind != NodeKind::RuleNode && n->kind != NodeKind::ScannerNode && n->kind != NodeKind::ActionNode) return false;
    if (n->read_only) return false;  // Defaults baseline is never editable
    if (n->scope == Scope::Repo && !m.repo_available) return false;
    n->enabled = !n->enabled;
    m.dirty = !pending_edits(m).empty();
    return true;
}

bool toggle_expand_focused(Model& m) {
    Node* n = visible_node_mut(m, m.cursor);
    if (!n) return false;
    if (n->kind == NodeKind::RuleNode || n->kind == NodeKind::ScannerNode)
        return toggle_focused_rule(m);
    n->expanded = !n->expanded;
    return true;
}

namespace {
// Recursively collect editable (non-read-only) rule/scanner leaves under a node.
void collect_editable_rules(Node& n, std::vector<Node*>& out) {
    if (n.kind == NodeKind::RuleNode || n.kind == NodeKind::ScannerNode) {
        if (!n.read_only) out.push_back(&n);
        return;
    }
    for (auto& c : n.children) collect_editable_rules(c, out);
}
void collect_editable_rules_const(const Node& n, std::vector<const Node*>& out) {
    if (n.kind == NodeKind::RuleNode || n.kind == NodeKind::ScannerNode) {
        if (!n.read_only) out.push_back(&n);
        return;
    }
    for (const auto& c : n.children) collect_editable_rules_const(c, out);
}
}  // namespace

GroupCount group_count(const Node& group) {
    GroupCount gc;
    std::vector<const Node*> leaves;
    collect_editable_rules_const(group, leaves);
    for (const auto* l : leaves) { ++gc.total; if (l->enabled) ++gc.enabled; }
    return gc;
}

bool toggle_focused_group(Model& m) {
    Node* n = visible_node_mut(m, m.cursor);
    if (!n) return false;
    if (n->kind == NodeKind::RuleNode || n->kind == NodeKind::ScannerNode)
        return toggle_focused_rule(m);
    if (n->read_only) return false;  // Defaults groups aren't editable
    if (n->scope == Scope::Repo && !m.repo_available) return false;

    std::vector<Node*> leaves;
    collect_editable_rules(*n, leaves);
    if (leaves.empty()) return false;
    // If everything is on, turn all off; otherwise turn all on. One intuitive
    // press cycles the whole group.
    bool all_on = true;
    for (const auto* l : leaves) if (!l->enabled) { all_on = false; break; }
    bool new_state = !all_on;
    for (auto* l : leaves) l->enabled = new_state;
    m.dirty = !pending_edits(m).empty();
    return true;
}

void rebaseline(Model& m) {
    struct RB {
        void walk(Node& n) {
            if (n.kind == NodeKind::RuleNode || n.kind == NodeKind::ScannerNode)
                n.baseline = n.enabled;
            else for (auto& c : n.children) walk(c);
        }
    };
    RB rb;
    for (auto& root : m.roots) rb.walk(root);
    m.dirty = false;
}

ScopeState desired_state(const Model& m, Scope scope) {
    ScopeState st;
    struct SW {
        Scope scope;
        ScopeState* out;
        void walk(const Node& n) {
            if (n.read_only) return;              // Defaults baseline: never persisted
            if (n.scope != scope && n.kind == NodeKind::ScopeNode) {
                // Only descend into the matching scope root.
                if (n.scope != scope) { /* skip other scope subtree */ }
            }
            if (n.kind == NodeKind::RuleNode) {
                if (n.scope == scope && !n.inherited && !n.enabled)
                    out->disabled_rules.push_back(n.rule_id);
            } else if (n.kind == NodeKind::ScannerNode) {
                if (n.scope == scope && !n.inherited)
                    out->scanner_enabled.emplace_back(n.rule_id, n.enabled);
            } else {
                for (const auto& c : n.children) walk(c);
            }
        }
    };
    SW sw{scope, &st};
    for (const auto& root : m.roots)
        if (root.scope == scope) sw.walk(root);   // only the target scope root
    return st;
}

}  // namespace keur::manager
