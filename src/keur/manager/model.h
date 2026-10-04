/**
 * @file manager/model.h
 * @brief Pure tree model + reducers for the `keur manager` TUI.
 *
 * The tree is Scope (Global / This repo) -> Category -> Rule. Navigation and
 * toggling are pure functions over the model (no I/O, no terminal), so the
 * whole interaction is unit-testable by feeding key events and asserting the
 * resulting state — exactly the "pure model + reducers" the design doc calls
 * for. The interactive loop (cmd/manager.cpp) owns the I/O edge: it reads keys
 * (platform::read_key) and persists the ChangeSet (via the shared rules
 * persister). This module never writes files.
 *
 * @see docs/design/keur-manager-tree-tui.md
 * @implements ADR-003
 * @id KEUR-MANAGER-MODEL
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace keur::manager {

/// Which config scope a toggle in this subtree writes to. Defaults is the
/// shipped built-in template — shown read-only as the baseline, never written.
enum class Scope { Defaults, Global, Repo };

/// A grouping facet — which tag a tree level groups by. The manager can reorder
/// these to view the same rules category-first, language-first, etc.
enum class Facet { Category, Subcategory, Language, Framework };

/// A node kind. The tree has scope roots, category groups, rule leaves, and —
/// under Defaults — read-only setting leaves (scanner/policy key = value), plus
/// scanner leaves (toggleable external tools) and action leaves (run a command,
/// e.g. chain/unchain keur into the repo's hook).
enum class NodeKind { ScopeNode, CategoryNode, RuleNode, SettingNode, ScannerNode, ActionNode };

/// One node in the flattened tree. Children are held by index ranges resolved
/// at render/navigation time via `visible_rows`, so the model stays a simple
/// nested vector rather than a pointer graph.
struct Node {
    NodeKind    kind;
    std::string label;        ///< display text (scope name / category / rule id)
    Scope       scope;        ///< owning scope (inherited down the branch)
    std::string rule_id;      ///< RuleNode only: the id to toggle
    std::string detail;       ///< RuleNode/SettingNode: severity/title or value
    bool        enabled = true;  ///< RuleNode only: on/off (false = disabled)
    bool        baseline = true; ///< RuleNode only: state at build time (for diff)
    bool        read_only = false; ///< true under Defaults: toggles are ignored
    bool        deprecated = false; ///< RuleNode only: marked, pending removal
    bool        inherited = false; ///< RuleNode (global): overruled by a more specific scope (repo) → greyed
    bool        on_path = true;    ///< ScannerNode only: is the tool available on PATH?
    std::string hint;              ///< ScannerNode only: install hint when not on PATH
    bool        warn = false;      ///< SettingNode (Hooks): problem state → red ⚠
    std::string action;            ///< ActionNode only: "augment" | "unaugment"
    bool        expanded = true; ///< ScopeNode/CategoryNode: is it open?
    int         depth = 0;    ///< 0 scope, 1 category, 2 rule — for indenting
    std::vector<Node> children;
};

/// The whole editable tree + cursor + dirty flag. Pure data.
struct Model {
    std::vector<Node> roots;   ///< the Scope nodes (Global, This repo)
    int  cursor = 0;           ///< index into visible_rows()
    bool dirty = false;        ///< unsaved toggles since last save
    bool repo_available = true;///< false when not inside a git repo (repo scope inert)
    std::vector<Facet> order;  ///< current facet grouping order (for the header)
};

/// One pending edit — a rule or scanner toggled in a scope. The list of these
/// IS the ChangeSet the loop persists on save. `disable == true` means "off".
struct Edit {
    Scope       scope;
    std::string id;         ///< rule id or scanner name
    bool        disable;
    bool        is_scanner = false;  ///< true → write [scanners.x] enabled; false → [rules] disabled
};

/// A flattened, currently-visible row (respecting collapsed nodes). Returned by
/// `visible_rows` and used by navigation + the view. `node` points into the
/// Model's tree (valid until the tree is mutated).
struct Row {
    const Node* node;

    // Explicit constructor so vector::emplace_back works on every toolchain:
    // libc++ (macOS Xcode CI) cannot emplace a constructor-less aggregate.
    // @see KEUR-PORT-007
    explicit Row(const Node* node_) : node(node_) {}
};

// --- Build ------------------------------------------------------------------

/// One rule as it should appear in the tree, with its 4 grouping FACETS + the
/// current on/off state. The caller (cmd/manager) builds these from the baked
/// catalogue + the resolved disabled-set per scope.
struct RuleItem {
    std::string category;
    std::string subcategory;
    std::string language;
    std::string framework;
    std::string id;
    std::string detail;   ///< e.g. "[critical] private-key"
    bool        enabled;
    bool        deprecated = false;  ///< shown with a marker; removed in a later version
    bool        inherited = false;   ///< overruled by a more specific scope (repo) → greyed
};

/// The default grouping order: category → subcategory → language → framework.
[[nodiscard]] std::vector<Facet> default_facet_order();

/// The next grouping order in the cycle, for the "reorder" key. Cycles through
/// a small set of useful presets (category-first ↔ language-first, …). Pure.
[[nodiscard]] std::vector<Facet> next_facet_order(const std::vector<Facet>& current);

/// Human name of a facet (for the header "grouped by …" hint).
[[nodiscard]] std::string facet_name(Facet f);

/// The value of a facet on a rule item (e.g. Facet::Language → item.language).
[[nodiscard]] std::string facet_value(const RuleItem& item, Facet f);

/// A read-only line under the Hooks branch: what manages hooks in this scope
/// and whether keur is effective/shadowed/augmented. Label + value, dimmed —
/// unless `warn` marks it a problem (shadowed / not chained), shown red with ⚠.
struct HookInfoItem {
    std::string label;   ///< e.g. "manager", "keur global", "keur chained"
    std::string value;   ///< e.g. "husky", "shadowed by husky", "yes"
    bool        warn = false;  ///< problem state → red ⚠ so it stands out
    std::string action;  ///< non-empty → an ActionNode: "augment" | "unaugment"

    HookInfoItem() = default;
    // Explicit constructor so vector::emplace_back works on every toolchain:
    // libc++ (macOS Xcode CI) cannot emplace a constructor-less aggregate.
    // @see KEUR-PORT-007
    HookInfoItem(std::string label_, std::string value_, bool warn_ = false,
                 std::string action_ = {})
        : label(std::move(label_)), value(std::move(value_)),
          warn(warn_), action(std::move(action_)) {}
};

/// An external scanner (gitleaks/semgrep/…) as a toggleable tree leaf, with its
/// PATH availability and an install hint when it is missing.
struct ScannerItem {
    std::string name;         ///< "gitleaks"
    std::string group;        ///< "secrets" | "sast" | … (shown as detail)
    bool        enabled;      ///< on/off in this scope
    bool        on_path;      ///< resolved: is the tool installed?
    std::string install_hint; ///< OS-specific hint when not on PATH ("" = none)
    bool        inherited = false;  ///< overruled by a more specific scope → greyed
};

/// One read-only setting shown under the Defaults branch: a key and its value
/// from the shipped template (e.g. "block-at" = "0.85", "gitleaks" = "block").
struct SettingItem {
    std::string group;   ///< sub-group under Defaults ("Scanners", "Policy")
    std::string label;   ///< the setting name
    std::string value;   ///< its value, shown dimmed

    SettingItem() = default;
    // Explicit constructor so vector::emplace_back works on every toolchain:
    // libc++ (macOS Xcode CI) cannot emplace a constructor-less aggregate.
    // @see KEUR-PORT-007
    SettingItem(std::string group_, std::string label_, std::string value_)
        : group(std::move(group_)), label(std::move(label_)), value(std::move(value_)) {}
};

/// The read-only Defaults branch: the shipped template's settings + the full
/// rule catalogue as the baseline (every rule shown "on"). This is the "what
/// keur ships with" view; Global/Repo layer their overrides on top.
struct DefaultsInfo {
    std::vector<SettingItem> settings;    ///< scanners/policy from the template
    std::vector<RuleItem>    catalog;     ///< the full rule catalogue (all on)
};

/// Build the tree. `defaults` is the read-only baseline branch (shipped
/// template settings + full rule catalogue). `global_rules`/`repo_rules` are the
/// resolved rule lists per editable scope (same catalogue, each showing its own
/// on/off state from that scope's disabled-list). `global_path`/`repo_path` are
/// the keur.toml locations shown on each scope header (empty = none/absent).
/// `order` is the facet grouping order (category-first by default); each scope's
/// rules are grouped into a variable-depth subtree by that facet sequence.
/// `repo_available` false hides the repo branch. Pure.
[[nodiscard]] Model build_model(const DefaultsInfo& defaults,
                                const std::vector<RuleItem>& global_rules,
                                const std::vector<RuleItem>& repo_rules,
                                bool repo_available,
                                const std::string& global_path = "",
                                const std::string& repo_path = "",
                                const std::vector<Facet>& order = default_facet_order(),
                                const std::vector<ScannerItem>& global_scanners = {},
                                const std::vector<ScannerItem>& repo_scanners = {},
                                const std::vector<HookInfoItem>& global_hooks = {},
                                const std::vector<HookInfoItem>& repo_hooks = {});

// --- Navigation / queries (pure) --------------------------------------------

/// The currently-visible rows top-to-bottom (collapsed subtrees excluded).
[[nodiscard]] std::vector<Row> visible_rows(const Model& m);

/// The node under the cursor, or nullptr when the tree is empty.
[[nodiscard]] const Node* focused(const Model& m);

/// If the cursor is on an ActionNode, return its action ("augment"|"unaugment");
/// otherwise "". Lets the loop run a command instead of a state toggle. Pure.
[[nodiscard]] std::string focused_action(const Model& m);

// --- Reducers (pure: return a new/mutated Model, no I/O) ---------------------

/// Move the cursor by `delta` rows, clamped to the visible range.
void move_cursor(Model& m, int delta);

/// Jump the cursor to the first / last visible row.
void cursor_top(Model& m);
void cursor_bottom(Model& m);

/// Expand the focused group node (no-op on a leaf). Returns true if it changed.
bool expand_focused(Model& m);

/// Collapse the focused group; on an already-collapsed or leaf node, jump the
/// cursor to its parent (file-tree convention). Returns true if it changed.
bool collapse_focused(Model& m);

/// Toggle expand/collapse on a group node; on a leaf, toggles enabled (same as
/// toggle_focused_rule). Returns true if it changed anything.
bool toggle_expand_focused(Model& m);

/// Flip the focused rule on/off. No-op unless the cursor is on a RuleNode in an
/// available scope. Sets `dirty`. Returns true if a rule was toggled.
bool toggle_focused_rule(Model& m);

/// Toggle the focused group: on a group node (Scope/Category), flip ALL its
/// editable rule descendants to the same new state (all-on → all-off, else
/// all-on). No-op on read-only groups. On a leaf, toggles that rule. Sets
/// `dirty`. Returns true if anything changed.
bool toggle_focused_group(Model& m);

/// After a successful save, make the current on/off state the new baseline
/// in-place (so `pending_edits` empties and `dirty` clears) WITHOUT rebuilding
/// the tree — the expand-state and cursor are preserved. Pure over the model.
void rebaseline(Model& m);

/// Recompute the `inherited` (overruled-by-repo) flag on every GLOBAL rule leaf
/// from the live model: a global rule is `inherited` when the SAME id is OFF in
/// the repo scope (the more specific scope governs, ADR-039 §6). Pure, derived
/// from current state — call after any toggle so the global row greys out live,
/// not only after a disk round-trip. No-op when the repo scope is unavailable.
/// @id KEUR-MANAGER-MODEL.refresh-inherited
void refresh_inherited(Model& m);

/// The complete DESIRED state of one scope's editable items — the "plan" the
/// atomic apply writes in a single transaction (not many per-item edits):
///   - `disabled_rules`: rule ids that are OFF in this scope.
///   - `scanner_enabled`: scanner name → on/off (only the ones shown, so the
///     writer sets exactly these `[scanners.x] enabled`).
/// Derived purely from the current model, ignoring read-only (Defaults) nodes
/// and inherited/overruled rows.
struct ScopeState {
    std::vector<std::string>                       disabled_rules;
    std::vector<std::pair<std::string, bool>>      scanner_enabled;
};

/// Extract the desired ScopeState for a scope from the model. Pure.
[[nodiscard]] ScopeState desired_state(const Model& m, Scope scope);

/// Count of (enabled, total) editable rules under a group node. For the
/// `[enabled/total]` badge the view shows on Category/Scope rows. Pure.
struct GroupCount { int enabled = 0; int total = 0; };
[[nodiscard]] GroupCount group_count(const Node& group);

/// The pending edits: every rule whose current state differs from the baseline
/// captured at build time. This is the ChangeSet the loop persists. Pure.
[[nodiscard]] std::vector<Edit> pending_edits(const Model& m);

/// Human name of a scope, for display.
[[nodiscard]] std::string scope_name(Scope s);

}  // namespace keur::manager
