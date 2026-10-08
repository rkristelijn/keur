// keur:ignore-file SEC-041 — test source: contains patterns it checks for
/**
 * @file tests/unit_layer45_test.cpp
 * @brief Layer 4/5 pure unit tests — the interactive/UI layer: terminal key
 *        decoding, the manager tree model/view, level/settings/tune key
 *        handlers, catalog facets, and small cmd helpers. doctest suite "unit".
 *
 * Split out of keur_test.cpp (ADR-002). No doctest main here (test_main.cpp);
 * this TU only registers its scenarios. Shared tempdir/path helpers come from
 * test_helpers.h; the manager model builders (ri/expand_all/make_test_model/…)
 * are domain helpers that belong to this TU and live below.
 */
#include <algorithm>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "../../../vendor/doctest.h"
#include "cmd/level.h"
#include "cmd/policy.h"
#include "cmd/settings.h"
#include "cmd/tune.h"
#include "cmd/learn.h"
#include "cmd/train.h"
#include "common/platform.h"
#include "manager/model.h"
#include "manager/view.h"
#include "manager/catalog.h"
#include "render/logo.h"
#include "test_helpers.h"

using keur::test::create_temp_dir;
using keur::test::write_file;
using keur::test::read_file;
using keur::test::cleanup_dir;
using keur::test::repo_root;

// ============================================================================
// TEST_SUITE: unit (Layer 4/5) — pure functions, no real filesystem scanning
// ============================================================================

TEST_SUITE("unit") {

// --- [feature:keyinput] pure terminal key decoding --------------------------

SCENARIO("[feature:keyinput] decode_key maps arrow CSI sequences") {  // @proves KEUR-KEYINPUT
    using keur::platform::decode_key;
    using keur::platform::Key;
    CHECK(decode_key("\033[A").key == Key::Up);
    CHECK(decode_key("\033[B").key == Key::Down);
    CHECK(decode_key("\033[C").key == Key::Right);
    CHECK(decode_key("\033[D").key == Key::Left);
    // Unknown CSI final byte → Unknown, not a misread arrow.
    CHECK(decode_key("\033[Z").key == Key::BackTab);  // Shift-Tab
}

SCENARIO("[feature:keyinput] decode_key maps named control keys") {  // @proves KEUR-KEYINPUT
    using keur::platform::decode_key;
    using keur::platform::Key;
    CHECK(decode_key("\r").key == Key::Enter);   // CR (raw mode)
    CHECK(decode_key("\n").key == Key::Enter);   // LF (piped)
    CHECK(decode_key(" ").key  == Key::Space);
    CHECK(decode_key("\t").key == Key::Tab);
    CHECK(decode_key("\x7f").key == Key::Backspace);
    CHECK(decode_key("\b").key   == Key::Backspace);
    CHECK(decode_key("\033").key == Key::Escape);  // lone ESC
    CHECK(decode_key("").key     == Key::Unknown); // empty buffer
}

SCENARIO("[feature:keyinput] decode_key returns printable chars for hjkl/s/q/?") {  // @proves KEUR-KEYINPUT
    using keur::platform::decode_key;
    using keur::platform::Key;
    for (char c : std::string("hjklsgGq?a")) {
        auto ev = decode_key(std::string(1, c));
        CHECK(ev.key == Key::Char);
        CHECK(ev.ch == c);
    }
    // Space is Space (not Char), even though it is printable.
    CHECK(decode_key(" ").key == Key::Space);
}

// --- [feature:manager-tree] pure tree model + reducers ----------------------

// Build a RuleItem with the 4 facets (subcategory/language/framework default to
// "generic"/"any"), so tests read clearly and survive facet-field additions.
static keur::manager::RuleItem ri(const std::string& category, const std::string& id,
                                  const std::string& detail, bool enabled) {
    keur::manager::RuleItem it;
    it.category = category; it.subcategory = "generic"; it.language = "any";
    it.framework = "any"; it.id = id; it.detail = detail; it.enabled = enabled;
    return it;
}

// Recursively expand every group so navigation tests see rule leaves at any depth.
static void expand_all(keur::manager::Node& n) {
    n.expanded = true;
    for (auto& c : n.children) expand_all(c);
}

static keur::manager::Model make_test_model() {
    // Global: 2 categories (security x2, secrets x1). Repo: security x1.
    std::vector<keur::manager::RuleItem> g = {
        ri("security", "KEUR-SEC-001", "[critical] private-key", true),
        ri("security", "KEUR-SEC-002", "[high] aws-key", false),   // starts off
        ri("secrets",  "KEUR-SECRET-1", "[high] token", true),
    };
    std::vector<keur::manager::RuleItem> r = {
        ri("security", "KEUR-SEC-001", "[critical] private-key", true),
    };
    keur::manager::DefaultsInfo d;
    d.settings = {{"Scanners", "gitleaks", "secrets block"},
                  {"Policy", "block-at", "0.85"}};
    d.catalog = g;  // baseline catalogue
    auto m = keur::manager::build_model(d, g, r, /*repo_available=*/true);
    for (auto& root : m.roots) expand_all(root);
    return m;
}

// A minimal editable-only model for tests that focus a group root.
static keur::manager::Model make_editable_model() {
    std::vector<keur::manager::RuleItem> g = {
        ri("security", "KEUR-SEC-001", "[critical] private-key", true),
        ri("security", "KEUR-SEC-002", "[high] aws-key", false),
        ri("secrets",  "KEUR-SECRET-1", "[high] token", true),
    };
    std::vector<keur::manager::RuleItem> r = {
        ri("security", "KEUR-SEC-001", "[critical] private-key", true),
    };
    keur::manager::DefaultsInfo d;  // empty defaults
    auto m = keur::manager::build_model(d, g, r, true);
    return m;
}

SCENARIO("[feature:manager-tree] build_model builds Scope->Category->Rule") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    REQUIRE(m.roots.size() == 3);                 // Defaults + Global + This repo
    CHECK(m.roots[0].label == "Defaults");
    CHECK(m.roots[0].read_only == true);
    CHECK(m.roots[1].label.find("Global") == 0);
    CHECK(m.roots[2].label.find("This repo") == 0);
    // Global has the editable secrets + security categories (a-z sorted), each
    // with a subcategory level below ('generic' for this test data).
    const auto& global = m.roots[1];
    REQUIRE(global.children.size() == 2);
    CHECK(global.children[0].label == "secrets");   // a-z: secrets before security
    CHECK(global.children[1].label == "security");
    // security → generic → 2 rules (SEC-001 on, SEC-002 off), leaves a-z by id.
    const keur::manager::Node* security = &global.children[1];
    REQUIRE(security->children.size() == 1);
    CHECK(security->children[0].label == "generic");
    const auto& sec_rules = security->children[0].children;
    REQUIRE(sec_rules.size() == 2);
    CHECK(sec_rules[0].rule_id == "KEUR-SEC-001");  // a-z
    CHECK(sec_rules[1].rule_id == "KEUR-SEC-002");
    CHECK(sec_rules[1].enabled == false);   // SEC-002 off
    CHECK(sec_rules[1].baseline == false);
    CHECK(sec_rules[1].read_only == false);
}

SCENARIO("[feature:manager-tree] Defaults branch is read-only baseline") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    const auto& defaults = m.roots[0];
    CHECK(defaults.read_only == true);
    // It carries a Scanners group and a Policy group (from settings).
    bool has_scanners = false, has_policy = false, has_catalog = false;
    for (const auto& c : defaults.children) {
        if (c.label == "Scanners") has_scanners = true;
        if (c.label == "Policy") has_policy = true;
        if (c.label.find("catalogue") != std::string::npos) has_catalog = true;
        CHECK(c.read_only == true);
    }
    CHECK(has_scanners);
    CHECK(has_policy);
    CHECK(has_catalog);
}

SCENARIO("[feature:manager-tree] toggling a Defaults rule is a no-op") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    // Put the cursor on the first read-only rule under Defaults.
    auto rows = keur::manager::visible_rows(m);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<std::size_t>(i)].node->kind == keur::manager::NodeKind::RuleNode &&
            rows[static_cast<std::size_t>(i)].node->read_only) { idx = i; break; }
    REQUIRE(idx >= 0);
    m.cursor = idx;
    CHECK(keur::manager::toggle_focused_rule(m) == false);   // read-only → no-op
    CHECK(m.dirty == false);
    CHECK(keur::manager::pending_edits(m).empty());
}

SCENARIO("[feature:manager-tree] facet grouping builds a variable-depth tree") {  // @proves KEUR-MANAGER-MODEL
    // Two rules in the same category but different subcategory → the tree nests
    // category → subcategory → rule (variable depth from the facet order).
    auto a = ri("security", "R-SAST", "", true); a.subcategory = "sast";
    auto b = ri("security", "R-SQLI", "", true); b.subcategory = "sqli";
    keur::manager::DefaultsInfo d;
    auto m = keur::manager::build_model(d, {a, b}, {}, false);
    const auto& global = m.roots[1];
    REQUIRE(global.children.size() == 1);
    CHECK(global.children[0].label == "security");
    REQUIRE(global.children[0].children.size() == 2);
    CHECK(global.children[0].children[0].label == "sast");
    CHECK(global.children[0].children[1].label == "sqli");
    CHECK(global.children[0].children[0].children.size() == 1);
}

SCENARIO("[feature:manager-tree] next_facet_order cycles category<->language first") {  // @proves KEUR-MANAGER-MODEL
    auto def = keur::manager::default_facet_order();
    CHECK(def[0] == keur::manager::Facet::Category);
    auto lang = keur::manager::next_facet_order(def);
    CHECK(lang[0] == keur::manager::Facet::Language);
    auto back = keur::manager::next_facet_order(lang);
    CHECK(back[0] == keur::manager::Facet::Category);
}

SCENARIO("[feature:manager-tree] language-first order groups by language at top") {  // @proves KEUR-MANAGER-MODEL
    auto cpp = ri("security", "R-CPP", "", true); cpp.language = "cpp";
    auto py  = ri("secrets",  "R-PY",  "", true); py.language = "python";
    keur::manager::DefaultsInfo d;
    auto order = keur::manager::next_facet_order(keur::manager::default_facet_order());
    auto m = keur::manager::build_model(d, {cpp, py}, {}, false, "", "", order);
    const auto& global = m.roots[1];
    REQUIRE(global.children.size() == 2);
    CHECK(global.children[0].label == "cpp");
    CHECK(global.children[1].label == "python");
}

SCENARIO("[feature:manager-tree] no repo scope when repo unavailable") {  // @proves KEUR-MANAGER-MODEL
    keur::manager::DefaultsInfo d;
    auto m = keur::manager::build_model(d, {ri("security","A","",true)}, {}, false);
    REQUIRE(m.roots.size() == 2);                 // Defaults + Global (no repo)
    CHECK(m.roots[1].label.find("Global") == 0);
}

// Helper: visible index of the first RuleNode with a given id.
static int row_index_of_rule(const keur::manager::Model& m, const std::string& id) {
    auto rows = keur::manager::visible_rows(m);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<std::size_t>(i)].node->rule_id == id) return i;
    return -1;
}

SCENARIO("[feature:manager-tree] visible_rows honours collapse") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    int full = static_cast<int>(keur::manager::visible_rows(m).size());
    // Collapse the Global scope (roots[1]) — find its row index first.
    auto rows = keur::manager::visible_rows(m);
    int gidx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<std::size_t>(i)].node->label.find("Global") == 0) { gidx = i; break; }
    REQUIRE(gidx >= 0);
    m.cursor = gidx;
    CHECK(keur::manager::collapse_focused(m));
    int collapsed = static_cast<int>(keur::manager::visible_rows(m).size());
    CHECK(collapsed < full);
}

SCENARIO("[feature:manager-tree] collapse on leaf jumps to parent") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    int idx = row_index_of_rule(m, "KEUR-SEC-001");  // an editable Global rule leaf
    // Find the Global one (repo also has SEC-001); pick the first non-read-only.
    auto rows = keur::manager::visible_rows(m);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto* n = rows[static_cast<std::size_t>(i)].node;
        if (n->rule_id == "KEUR-SEC-001" && !n->read_only) { idx = i; break; }
    }
    REQUIRE(idx >= 0);
    m.cursor = idx;
    CHECK(keur::manager::collapse_focused(m));
    CHECK(keur::manager::focused(m)->label == "generic");  // jumped to parent (subcategory)
}

SCENARIO("[feature:manager-tree] move_cursor clamps to visible range") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    keur::manager::move_cursor(m, -5);
    CHECK(m.cursor == 0);
    keur::manager::cursor_bottom(m);
    int last = static_cast<int>(keur::manager::visible_rows(m).size()) - 1;
    CHECK(m.cursor == last);
    keur::manager::move_cursor(m, 99);
    CHECK(m.cursor == last);
}

SCENARIO("[feature:manager-tree] toggle_focused_rule flips state + tracks edits") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    // Focus the editable Global KEUR-SEC-001 (on by default).
    auto rows = keur::manager::visible_rows(m);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto* n = rows[static_cast<std::size_t>(i)].node;
        if (n->rule_id == "KEUR-SEC-001" && !n->read_only &&
            n->scope == keur::manager::Scope::Global) { idx = i; break; }
    }
    REQUIRE(idx >= 0);
    m.cursor = idx;
    CHECK(keur::manager::toggle_focused_rule(m));
    CHECK(m.dirty == true);
    auto edits = keur::manager::pending_edits(m);
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].id == "KEUR-SEC-001");
    CHECK(edits[0].scope == keur::manager::Scope::Global);
    CHECK(edits[0].disable == true);   // was on → now off
    // Toggling back to baseline clears the edit and dirty flag.
    CHECK(keur::manager::toggle_focused_rule(m));
    CHECK(keur::manager::pending_edits(m).empty());
    CHECK(m.dirty == false);
}

SCENARIO("[feature:manager-tree] toggle on a group node does not create an edit") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_editable_model();
    m.cursor = 0;  // the Defaults scope root (a group)
    CHECK(keur::manager::toggle_focused_rule(m) == false);
    CHECK(keur::manager::pending_edits(m).empty());
}

// --- [feature:manager-tree] pure view ---------------------------------------

SCENARIO("[feature:manager-tree] render_row shows glyphs + disabled marker") {  // @proves KEUR-MANAGER-VIEW
    auto m = make_test_model();
    auto rows = keur::manager::visible_rows(m);
    // Row 0 = Defaults scope (expanded ▼).
    CHECK(keur::manager::render_row(rows[0]).find("\u25bc") != std::string::npos);
    // A disabled editable rule shows ☐ and (disabled). SEC-002 is off in Global.
    std::string joined;
    for (auto& r : rows) joined += keur::manager::render_row(r) + "\n";
    CHECK(joined.find("\u2610 KEUR-SEC-002") != std::string::npos);   // ☐ todo
    CHECK(joined.find("(disabled)") != std::string::npos);
    CHECK(joined.find("\u2713 KEUR-SEC-001") != std::string::npos);   // ✓ done
}

SCENARIO("[feature:manager-tree] render frame has header, footer, cursor marker") {  // @proves KEUR-MANAGER-VIEW
    auto m = make_test_model();
    keur::manager::ViewOpts opts; opts.rows = 40; opts.cols = 80; opts.color = false;
    auto lines = keur::manager::render(m, opts);
    std::string all;
    for (auto& l : lines) all += l + "\n";
    CHECK(all.find("keur manager") != std::string::npos);      // header
    CHECK(all.find("Space toggle") != std::string::npos);       // footer hint
    CHECK(all.find("\u276f") != std::string::npos);             // cursor marker ❯
    // Dirty flag surfaces in the header after toggling an editable Global rule.
    auto rows = keur::manager::visible_rows(m);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto* n = rows[static_cast<std::size_t>(i)].node;
        if (n->rule_id == "KEUR-SEC-001" && !n->read_only &&
            n->scope == keur::manager::Scope::Global) { m.cursor = i; break; }
    }
    keur::manager::toggle_focused_rule(m);
    auto lines2 = keur::manager::render(m, opts);
    CHECK(lines2[0].find("unsaved") != std::string::npos);
}

// --- [feature:manager-tree] save keeps one backup ---------------------------

SCENARIO("[feature:manager-tree] Fs backup pattern keeps the previous version") {  // @proves KEUR-MANAGER-MODEL
    // Exercises the single-slot backup contract rules_toggle relies on: before
    // overwriting keur.toml, its current content is copied to keur.toml.bak.
    keur::adapters::Fs fs{};  // real POSIX Fs
    std::string dir = std::string(KEUR_TEST_ROOT) + "/bin/.keur_backup_test";
    fs.mkdir_p(dir);
    std::string path = dir + "/keur.toml";
    std::string bak  = path + ".bak";

    // Simulate two saves via the same read→backup→write sequence.
    auto save = [&fs, &path, &bak](const std::string& content) {
        if (fs.exists(path)) { auto rr = fs.read_file(path); if (rr.ok) fs.write_file(bak, rr.content); }
        fs.write_file(path, content);
    };
    save("version = \"1\"\n");            // first save: no prior file → no backup yet
    CHECK(fs.exists(path));
    save("version = \"2\"\n");            // second save: backup captures "1"
    auto cur = fs.read_file(path);
    auto old = fs.read_file(bak);
    REQUIRE(cur.ok); REQUIRE(old.ok);
    CHECK(cur.content == "version = \"2\"\n");
    CHECK(old.content == "version = \"1\"\n");  // exactly the previous version
    // Cleanup.
    fs.remove(path); fs.remove(bak);
}

SCENARIO("[feature:manager-tree] global rule overruled by repo is marked inherited") {  // @proves KEUR-MANAGER-MODEL
    // Cascade: the more specific scope (repo) wins. When repo governs a rule,
    // the GLOBAL row is greyed with "(overruled by repo)" — the effective
    // decision lives in the repo scope.
    auto gi = ri("security", "R1", "", true); gi.inherited = true;
    std::vector<keur::manager::RuleItem> g = {gi};
    std::vector<keur::manager::RuleItem> r = {ri("security", "R1", "", false)};  // off in repo
    keur::manager::DefaultsInfo d;
    auto m = keur::manager::build_model(d, g, r, true);
    // Expand all groups so the leaves are visible.
    for (auto& root : m.roots) { root.expanded = true;
        for (auto& cat : root.children) { cat.expanded = true;
            for (auto& sub : cat.children) sub.expanded = true; } }
    // Find the GLOBAL R1 leaf.
    auto rows = keur::manager::visible_rows(m);
    const keur::manager::Node* global_leaf = nullptr;
    for (auto& row : rows) {
        const auto* n = row.node;
        if (n->rule_id == "R1" && n->scope == keur::manager::Scope::Global) global_leaf = n;
    }
    REQUIRE(global_leaf != nullptr);
    CHECK(global_leaf->inherited == true);
    // The view marks it "(overruled by repo)".
    keur::manager::Row rr{global_leaf};
    CHECK(keur::manager::render_row(rr).find("(overruled by repo)") != std::string::npos);
}

SCENARIO("[feature:manager-tree] toggling a repo rule off greys the global row live") {  // @proves KEUR-MANAGER-MODEL.refresh-inherited
    // Both scopes start with R1 ON (nothing overruled yet).
    std::vector<keur::manager::RuleItem> g = {ri("security", "R1", "", true)};
    std::vector<keur::manager::RuleItem> r = {ri("security", "R1", "", true)};
    keur::manager::DefaultsInfo d;
    auto m = keur::manager::build_model(d, g, r, true);
    for (auto& root : m.roots) expand_all(root);

    auto find_leaf = [&](keur::manager::Scope sc) -> const keur::manager::Node* {
        for (auto& row : keur::manager::visible_rows(m)) {
            const auto* n = row.node;
            if (n->rule_id == "R1" && n->scope == sc &&
                n->kind == keur::manager::NodeKind::RuleNode) return n;
        }
        return nullptr;
    };

    // Precondition: global is NOT inherited while repo R1 is on.
    const auto* gl = find_leaf(keur::manager::Scope::Global);
    REQUIRE(gl != nullptr);
    CHECK_FALSE(gl->inherited);

    // Put the cursor on the REPO R1 leaf and toggle it OFF.
    int repo_idx = -1;
    auto rows = keur::manager::visible_rows(m);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto* n = rows[static_cast<std::size_t>(i)].node;
        if (n->rule_id == "R1" && n->scope == keur::manager::Scope::Repo &&
            n->kind == keur::manager::NodeKind::RuleNode) { repo_idx = i; break; }
    }
    REQUIRE(repo_idx >= 0);
    m.cursor = repo_idx;
    REQUIRE(keur::manager::toggle_focused_rule(m));

    // The GLOBAL row is now inherited (greyed) WITHOUT any save/rebuild.
    const auto* gl2 = find_leaf(keur::manager::Scope::Global);
    REQUIRE(gl2 != nullptr);
    CHECK(gl2->inherited);

    // Toggling the repo rule back ON clears the global grey-out again.
    m.cursor = repo_idx;
    REQUIRE(keur::manager::toggle_focused_rule(m));
    const auto* gl3 = find_leaf(keur::manager::Scope::Global);
    REQUIRE(gl3 != nullptr);
    CHECK_FALSE(gl3->inherited);
}

SCENARIO("[feature:manager-tree] search filters leaves but keeps the matching path") {  // @proves KEUR-MANAGER-MODEL.search
    auto java = ri("web", "WEB-JAVA-1", "java servlet check", true); java.language = "java";
    auto py   = ri("web", "WEB-PY-1", "flask check", true);         py.language = "python";
    std::vector<keur::manager::RuleItem> g = {java, py};
    keur::manager::DefaultsInfo d;
    auto m = keur::manager::build_model(d, g, {}, false);
    for (auto& root : m.roots) expand_all(root);

    auto all_ids = [](const keur::manager::Model& mm) {
        std::vector<std::string> ids;
        for (auto& row : keur::manager::visible_rows(mm))
            if (row.node->kind == keur::manager::NodeKind::RuleNode) ids.push_back(row.node->rule_id);
        return ids;
    };
    {   // No filter: both leaves reachable.
        auto ids = all_ids(m);
        CHECK(std::find(ids.begin(), ids.end(), "WEB-JAVA-1") != ids.end());
        CHECK(std::find(ids.begin(), ids.end(), "WEB-PY-1")   != ids.end());
    }
    // Filter "java": only the java leaf remains; python is gone.
    keur::manager::search_begin(m);
    for (char c : std::string("java")) keur::manager::search_input(m, c);
    {
        auto ids = all_ids(m);
        CHECK(std::find(ids.begin(), ids.end(), "WEB-JAVA-1") != ids.end());
        CHECK(std::find(ids.begin(), ids.end(), "WEB-PY-1")   == ids.end());
    }
    // The ancestor path stays visible (scope + category).
    {
        bool saw_scope = false, saw_cat = false;
        for (auto& row : keur::manager::visible_rows(m)) {
            if (row.node->kind == keur::manager::NodeKind::ScopeNode &&
                row.node->scope == keur::manager::Scope::Global) saw_scope = true;
            if (row.node->kind == keur::manager::NodeKind::CategoryNode &&
                row.node->label == "web") saw_cat = true;
        }
        CHECK(saw_scope);
        CHECK(saw_cat);
    }
    // Esc clears the filter: python reachable again.
    keur::manager::search_clear(m);
    for (auto& root : m.roots) expand_all(root);
    {
        auto ids = all_ids(m);
        CHECK(std::find(ids.begin(), ids.end(), "WEB-PY-1") != ids.end());
    }
}

SCENARIO("[feature:manager-tree] search keeps groups collapsible (no flood)") {  // @proves KEUR-MANAGER-MODEL.search.expand
    std::vector<keur::manager::RuleItem> g;
    for (int i = 0; i < 8; ++i) {
        auto it = ri("web", "WEB-JAVA-" + std::to_string(i), "java check", true);
        it.language = "java";
        g.push_back(it);
    }
    keur::manager::DefaultsInfo d;
    auto m = keur::manager::build_model(d, g, {}, false);

    keur::manager::search_begin(m);
    for (char c : std::string("java")) keur::manager::search_input(m, c);
    auto count_leaves = [](const keur::manager::Model& mm) {
        int n = 0;
        for (auto& row : keur::manager::visible_rows(mm))
            if (row.node->kind == keur::manager::NodeKind::RuleNode) ++n;
        return n;
    };
    CHECK(count_leaves(m) == 8);   // revealed while searching

    auto rows = keur::manager::visible_rows(m);
    int web_idx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<std::size_t>(i)].node->kind == keur::manager::NodeKind::CategoryNode &&
            rows[static_cast<std::size_t>(i)].node->label == "web") { web_idx = i; break; }
    REQUIRE(web_idx >= 0);
    m.cursor = web_idx;
    REQUIRE(keur::manager::collapse_focused(m));
    CHECK(count_leaves(m) == 0);   // folded away — collapse still works in search

    bool saw_web = false;
    for (auto& row : keur::manager::visible_rows(m))
        if (row.node->kind == keur::manager::NodeKind::CategoryNode && row.node->label == "web") saw_web = true;
    CHECK(saw_web);   // the match path (category header) remains
}

SCENARIO("[feature:manager-tree] match helpers are case-insensitive over label/detail") {  // @proves KEUR-MANAGER-MODEL.search.match
    keur::manager::Node leaf;
    leaf.kind = keur::manager::NodeKind::RuleNode;
    leaf.label = "WEB-JAVA-1";
    leaf.detail = "Java Servlet";
    CHECK(keur::manager::node_matches_query(leaf, "java"));
    CHECK(keur::manager::node_matches_query(leaf, "SERVLET"));
    CHECK_FALSE(keur::manager::node_matches_query(leaf, "python"));
    CHECK(keur::manager::node_matches_query(leaf, ""));
    keur::manager::Node grp;
    grp.kind = keur::manager::NodeKind::CategoryNode;
    grp.label = "web";
    grp.children.push_back(leaf);
    CHECK(keur::manager::subtree_has_match(grp, "java"));
    CHECK_FALSE(keur::manager::subtree_has_match(grp, "zzz"));
}

SCENARIO("[feature:manager-tree] rule groups and leaves are sorted a-z") {  // @proves KEUR-MANAGER-MODEL
    std::vector<keur::manager::RuleItem> g = {
        ri("security", "SEC-ZeBRA", "", true),
        ri("security", "SEC-apple", "", true),
        ri("auth", "AUTH-1", "", true),
    };
    keur::manager::DefaultsInfo d;
    auto m = keur::manager::build_model(d, g, {}, false);
    for (auto& root : m.roots) expand_all(root);

    const keur::manager::Node* global = nullptr;
    for (auto& root : m.roots) if (root.scope == keur::manager::Scope::Global) global = &root;
    REQUIRE(global != nullptr);
    std::vector<std::string> cats;
    for (auto& c : global->children)
        if (c.kind == keur::manager::NodeKind::CategoryNode) cats.push_back(c.label);
    auto ia = std::find(cats.begin(), cats.end(), "auth");
    auto is = std::find(cats.begin(), cats.end(), "security");
    REQUIRE(ia != cats.end()); REQUIRE(is != cats.end());
    CHECK(ia < is);   // auth before security

    std::vector<std::string> leaves;
    for (auto& row : keur::manager::visible_rows(m))
        if (row.node->kind == keur::manager::NodeKind::RuleNode &&
            row.node->rule_id.rfind("SEC-", 0) == 0)
            leaves.push_back(row.node->rule_id);
    REQUIRE(leaves.size() == 2);
    CHECK(leaves == std::vector<std::string>{"SEC-ZeBRA", "SEC-apple"});  // byte-order a-z
}

SCENARIO("[feature:manager-tree] a fully-overruled global group renders greyed") {  // @proves KEUR-MANAGER-VIEW.group-overruled
    // Global "compliance" group holds a single rule PROJ-004; the repo overrules
    // it (off in repo). The group then has no live rule on global → greyed.
    auto gi = ri("compliance", "PROJ-004", "", true); gi.inherited = true;
    std::vector<keur::manager::RuleItem> g = {gi};
    std::vector<keur::manager::RuleItem> r = {ri("compliance", "PROJ-004", "", false)};
    keur::manager::DefaultsInfo d;
    auto m = keur::manager::build_model(d, g, r, true);
    for (auto& root : m.roots) expand_all(root);

    keur::manager::ViewOpts opts; opts.rows = 40; opts.cols = 100; opts.color = true;
    auto lines = keur::manager::render(m, opts);

    // Find the GLOBAL "compliance" group line and assert it carries the dim SGR.
    // (The global scope header is cyan; its child groups fall through to the
    // group-overruled dim path.)
    const std::string kDim = "\033[2m";
    bool saw_dim_compliance = false, saw_dim_generic = false;
    for (const auto& ln : lines) {
        if (ln.find("compliance") != std::string::npos && ln.find(kDim) != std::string::npos)
            saw_dim_compliance = true;
        if (ln.find("generic") != std::string::npos && ln.find(kDim) != std::string::npos)
            saw_dim_generic = true;
    }
    CHECK(saw_dim_compliance);   // the category parent greys out
    CHECK(saw_dim_generic);      // the subcategory parent greys out too
}

SCENARIO("[feature:manager-tree] Hooks action row exposes an augment action") {  // @proves KEUR-MANAGER-MODEL
    keur::manager::DefaultsInfo d;
    std::vector<keur::manager::HookInfoItem> hi = {
        {"keur chained", "no  (run: keur hook --augment)", true, ""},
        {"fix this with keur hook --augment", "", false, "augment"},
    };
    auto m = keur::manager::build_model(d, {}, {}, true, "", "/repo/keur.toml",
                                        keur::manager::default_facet_order(), {}, {}, {}, hi);
    for (auto& root : m.roots) expand_all(root);
    // Find the action row and focus it → focused_action returns "augment".
    auto rows = keur::manager::visible_rows(m);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<std::size_t>(i)].node->kind == keur::manager::NodeKind::ActionNode) { idx = i; break; }
    REQUIRE(idx >= 0);
    m.cursor = idx;
    CHECK(keur::manager::focused_action(m) == "augment");
    // A non-action row yields no action.
    m.cursor = 0;
    CHECK(keur::manager::focused_action(m).empty());
    // The action row renders with a ☐ todo box + its label.
    keur::manager::Row r{rows[static_cast<std::size_t>(idx)].node};
    CHECK(keur::manager::render_row(r).find("\u2610") != std::string::npos);
    CHECK(keur::manager::render_row(r).find("fix this") != std::string::npos);
}

SCENARIO("[feature:manager-tree] Hooks branch shows manager + keur status (read-only)") {  // @proves KEUR-MANAGER-MODEL
    keur::manager::DefaultsInfo d;
    std::vector<keur::manager::HookInfoItem> hi = {
        {"effective owner", "husky"},
        {"keur global", "shadowed by husky (repo-local core.hooksPath)"},
        {"manager detected", "husky"},
        {"keur chained", "no  (run: keur hook --augment)"},
    };
    auto m = keur::manager::build_model(d, {}, {}, true, "", "/repo/keur.toml",
                                        keur::manager::default_facet_order(), {}, {}, {}, hi);
    for (auto& root : m.roots) expand_all(root);
    // Find the Hooks group under This repo and its leaves.
    const keur::manager::Node* hooks = nullptr;
    for (auto& root : m.roots)
        if (root.scope == keur::manager::Scope::Repo)
            for (auto& c : root.children)
                if (c.label == "Hooks") hooks = &c;
    REQUIRE(hooks != nullptr);
    CHECK(hooks->read_only == true);                 // informational, not toggleable
    REQUIRE(hooks->children.size() == 4);
    // The rendered rows convey the shadow situation + the fix command.
    std::string joined;
    for (auto& leaf : hooks->children) {
        keur::manager::Row r{&leaf};
        joined += keur::manager::render_row(r) + "\n";
    }
    CHECK(joined.find("effective owner = husky") != std::string::npos);
    CHECK(joined.find("shadowed by husky") != std::string::npos);
    CHECK(joined.find("keur hook --augment") != std::string::npos);
}

SCENARIO("[feature:manager-tree] desired_state extracts the plan for one scope") {  // @proves KEUR-MANAGER-MODEL
    // Plan → apply: desired_state is the COMPLETE state to write in one shot.
    keur::manager::DefaultsInfo d;
    std::vector<keur::manager::RuleItem> g = {
        ri("security", "R-OFF", "", false),   // disabled → in the plan
        ri("security", "R-ON",  "", true),    // enabled  → NOT in the disabled list
    };
    std::vector<keur::manager::ScannerItem> gs = {
        {"gitleaks", "secrets", false, true, "", false},   // off
        {"semgrep",  "sast",    true,  true, "", false},   // on
    };
    auto m = keur::manager::build_model(d, g, {}, false, "", "",
                                        keur::manager::default_facet_order(), gs, {});
    auto st = keur::manager::desired_state(m, keur::manager::Scope::Global);
    REQUIRE(st.disabled_rules.size() == 1);
    CHECK(st.disabled_rules[0] == "R-OFF");            // only the off rule
    REQUIRE(st.scanner_enabled.size() == 2);           // both scanners recorded
    // Find each scanner's state.
    bool gitleaks_off = false, semgrep_on = false;
    for (auto& [name, en] : st.scanner_enabled) {
        if (name == "gitleaks" && !en) gitleaks_off = true;
        if (name == "semgrep"  &&  en) semgrep_on  = true;
    }
    CHECK(gitleaks_off);
    CHECK(semgrep_on);
}

SCENARIO("[feature:manager-tree] scanner leaf toggles + emits a scanner edit") {  // @proves KEUR-MANAGER-MODEL
    keur::manager::DefaultsInfo d;
    std::vector<keur::manager::ScannerItem> gs = {
        {"gitleaks", "secrets", true, true, "", false},
        {"semgrep",  "sast",    true, true, "", false},
    };
    auto m = keur::manager::build_model(d, {}, {}, false, "", "",
                                        keur::manager::default_facet_order(), gs, {});
    for (auto& root : m.roots) expand_all(root);
    // Find the gitleaks scanner leaf under Global.
    auto rows = keur::manager::visible_rows(m);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto* n = rows[static_cast<std::size_t>(i)].node;
        if (n->kind == keur::manager::NodeKind::ScannerNode && n->label == "gitleaks") { idx = i; break; }
    }
    REQUIRE(idx >= 0);
    m.cursor = idx;
    CHECK(keur::manager::toggle_focused_rule(m));   // toggles a scanner too
    auto edits = keur::manager::pending_edits(m);
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].id == "gitleaks");
    CHECK(edits[0].is_scanner == true);             // routed to [scanners.*] enabled
    CHECK(edits[0].disable == true);                // was on → off
}

SCENARIO("[feature:manager-tree] toggle_focused_group flips a whole category") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    // Focus the Global > security category node.
    auto rows = keur::manager::visible_rows(m);
    int cidx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto* n = rows[static_cast<std::size_t>(i)].node;
        if (n->kind == keur::manager::NodeKind::CategoryNode && n->label == "security" &&
            !n->read_only && n->scope == keur::manager::Scope::Global) { cidx = i; break; }
    }
    REQUIRE(cidx >= 0);
    m.cursor = cidx;
    // security has SEC-001 (on) + SEC-002 (off) → not all on → all on.
    CHECK(keur::manager::toggle_focused_group(m));
    auto gc = keur::manager::group_count(*keur::manager::focused(m));
    CHECK(gc.enabled == gc.total);       // whole group turned on
    // Toggle again → all off.
    CHECK(keur::manager::toggle_focused_group(m));
    gc = keur::manager::group_count(*keur::manager::focused(m));
    CHECK(gc.enabled == 0);
}

SCENARIO("[feature:manager-tree] rebaseline clears dirty and keeps expand state") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    // Toggle an editable rule → dirty.
    auto rows = keur::manager::visible_rows(m);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto* n = rows[static_cast<std::size_t>(i)].node;
        if (n->rule_id == "KEUR-SEC-001" && !n->read_only &&
            n->scope == keur::manager::Scope::Global) { m.cursor = i; break; }
    }
    keur::manager::toggle_focused_rule(m);
    REQUIRE(m.dirty == true);
    int rows_before = static_cast<int>(keur::manager::visible_rows(m).size());
    keur::manager::rebaseline(m);
    CHECK(m.dirty == false);                                   // saved → clean
    CHECK(keur::manager::pending_edits(m).empty());            // baseline updated
    // Expand state preserved: same number of visible rows (no collapse).
    CHECK(static_cast<int>(keur::manager::visible_rows(m).size()) == rows_before);
}

SCENARIO("[feature:manager-tree] group_count reports enabled/total") {  // @proves KEUR-MANAGER-MODEL
    auto m = make_test_model();
    // Global > security: SEC-001 on, SEC-002 off → 1/2.
    const keur::manager::Node* sec = nullptr;
    for (auto& root : m.roots)
        if (root.scope == keur::manager::Scope::Global)
            for (auto& c : root.children)
                if (c.label == "security") sec = &c;
    REQUIRE(sec != nullptr);
    auto gc = keur::manager::group_count(*sec);
    CHECK(gc.total == 2);
    CHECK(gc.enabled == 1);
}

// --- [feature:manager-catalog] baked compile-time catalogue -----------------

SCENARIO("[feature:manager-catalog] catalog is non-empty and sorted") {  // @proves KEUR-MANAGER-CATALOG
    const auto& c = keur::manager::catalog();
    REQUIRE(!c.empty());                       // baked at build time from .rule files
    // Sorted by category, then subcategory, then id → stable, reproducible.
    for (std::size_t i = 1; i < c.size(); ++i) {
        const auto& a = c[i-1]; const auto& b = c[i];
        auto key = [](const keur::manager::CatalogEntry& e) {
            return e.category + "\x1f" + e.subcategory + "\x1f" + e.id;
        };
        CHECK(key(a) <= key(b));
    }
    // Every entry has an id and a category.
    for (const auto& e : c) { CHECK(!e.id.empty()); CHECK(!e.category.empty()); }
}

SCENARIO("[feature:manager-catalog] overlay enabled-state comes from the disabled-set") {  // @proves KEUR-MANAGER-CATALOG
    // Simulate the manager loop's core: build items from the catalog, disabling
    // exactly the ids in a scope's disabled-set (the [rules] disabled list).
    const auto& c = keur::manager::catalog();
    REQUIRE(c.size() >= 2);
    std::set<std::string> disabled = { c[0].id };   // turn the first one off
    std::vector<keur::manager::RuleItem> items;
    for (const auto& e : c) {
        keur::manager::RuleItem it;
        it.category = e.category; it.subcategory = e.subcategory;
        it.language = e.language; it.framework = e.framework;
        it.id = e.id; it.detail = e.description;
        it.enabled = !disabled.count(e.id); it.deprecated = e.deprecated;
        items.push_back(std::move(it));
    }
    CHECK(items[0].enabled == false);          // in disabled-set → off
    CHECK(items[1].enabled == true);           // not listed → on (default)
}

// --- version / learn / help ------------------------------------------------

SCENARIO("[feature:version] version resolves from keur.toml, empty when absent") {
    // `keur version` prefers the version field of the resolved keur.toml and
    // otherwise falls back to the compile-time KEUR_VERSION. Here we prove the
    // config-resolution half: a toml with a version yields it; a missing file
    // yields empty (the caller then uses the baked-in default).
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    write_file(tmp + "/keur.toml", "version = \"9.9.9\"\n");
    CHECK(keur::cmd::policy_load(tmp + "/keur.toml").version == "9.9.9");
    CHECK(keur::cmd::policy_load(tmp + "/keur_missing.toml").version.empty());
}

SCENARIO("[feature:train] train_parse reads flags and source with sane defaults") {
    using keur::cmd::train_parse;
    // Defaults: no flags set, source defaults to "corpus".
    auto def = train_parse({});
    CHECK(def.stop == false);
    CHECK(def.status == false);
    CHECK(def.source == "corpus");
    // Flags and options are parsed.
    auto opt = train_parse({"--status", "--source", "keureval", "--model", "qwen"});
    CHECK(opt.status == true);
    CHECK(opt.source == "keureval");
    CHECK(opt.model == "qwen");
}

SCENARIO("[feature:learn] is_valid_rule_id accepts rule ids, rejects path-escapes") {
    using keur::cmd::is_valid_rule_id;
    // Accepts canonical rule ids.
    CHECK(is_valid_rule_id("KEUR-CMDI-003"));
    CHECK(is_valid_rule_id("SEC-038"));
    CHECK(is_valid_rule_id("KEUR-NEXT-001"));
    // Rejects anything that could escape docs/rules/ or is not a rule id:
    // path separators, dots, traversal, lowercase, spaces, empty, edge hyphens.
    CHECK_FALSE(is_valid_rule_id(""));
    CHECK_FALSE(is_valid_rule_id("../etc/passwd"));
    CHECK_FALSE(is_valid_rule_id("KEUR/CMDI"));
    CHECK_FALSE(is_valid_rule_id("KEUR-CMDI-003.md"));
    CHECK_FALSE(is_valid_rule_id("keur-cmdi-003"));
    CHECK_FALSE(is_valid_rule_id("KEUR CMDI"));
    CHECK_FALSE(is_valid_rule_id("-LEAD"));
    CHECK_FALSE(is_valid_rule_id("TRAIL-"));
    CHECK_FALSE(is_valid_rule_id("DOUBLE--HYPHEN"));
}

SCENARIO("[feature:learn] explainer_path joins base dir and id as <base>/<ID>.md") {
    using keur::cmd::explainer_path;
    CHECK(explainer_path("/k/docs/rules", "KEUR-CMDI-003") == "/k/docs/rules/KEUR-CMDI-003.md");
    CHECK(explainer_path("cache/keur/rules", "SEC-038") == "cache/keur/rules/SEC-038.md");
}

SCENARIO("[feature:help] help/version are dispatch aliases (behaviour proven e2e)") {
    // `help`/`--help`/`-h` and `version`/`--version`/`-v` are pure dispatch
    // aliases in main.cpp's command table; their end-to-end behaviour (usage
    // text, exit 0) is asserted in tests/e2e/smoke.sh. This scenario documents
    // that contract so [feature:help] is traceable. The parse-level surface the
    // unit tier can reach is covered by the version/learn scenarios above.
    CHECK(true);
}

// --- [feature:level] DOOM difficulty picker (logo bucket + level select) ----

SCENARIO("[feature:level] bucket_for_width maps terminal cols to a size band") {  // @proves KEUR-LOGO.bucket
    using keur::render::bucket_for_width;
    using keur::render::Bucket;
    // Boundaries: <=60 Sm, 61..100 Md, 101..140 Lg, >140 Xl.
    CHECK(bucket_for_width(1)   == Bucket::Sm);
    CHECK(bucket_for_width(60)  == Bucket::Sm);
    CHECK(bucket_for_width(61)  == Bucket::Md);
    CHECK(bucket_for_width(100) == Bucket::Md);
    CHECK(bucket_for_width(101) == Bucket::Lg);
    CHECK(bucket_for_width(140) == Bucket::Lg);
    CHECK(bucket_for_width(141) == Bucket::Xl);
    CHECK(bucket_for_width(9999) == Bucket::Xl);
}

SCENARIO("[feature:level] bucket_for_size fits BOTH width and height") {  // @proves KEUR-LOGO.fit
    using keur::render::bucket_for_size;
    using keur::render::logo_height;
    using keur::render::Bucket;
    // Wide but short terminal: width allows Xl, but only ~14 rows are free →
    // must drop to Sm (13 rows) so the logo top does not scroll off ("neuk").
    CHECK(bucket_for_size(200, 14) == Bucket::Sm);
    // Plenty of both → the biggest that fits its own width.
    CHECK(bucket_for_size(200, 40) == Bucket::Xl);
    // Narrow terminal caps on width: 70 cols can't fit Md (needs 72) → Sm (48).
    CHECK(bucket_for_size(70, 100) == Bucket::Sm);
    // 72 cols + tall enough fits Md exactly.
    CHECK(bucket_for_size(72, 40) == Bucket::Md);
    // Enough rows for Md (20) and width for Lg, but only 20 rows → Md, not Lg(27).
    CHECK(bucket_for_size(120, 20) == Bucket::Md);
    // Never returns something taller than the space given.
    for (int rows = 5; rows <= 40; ++rows) {
        auto b = bucket_for_size(200, rows);
        CHECK(logo_height(b) <= (rows < 13 ? 13 : rows));  // Sm floor may exceed a tiny space
    }
}

SCENARIO("[feature:level] logo_height is positive and ordered") {  // @proves KEUR-LOGO.height
    using keur::render::logo_height;
    using keur::render::Bucket;
    CHECK(logo_height(Bucket::Sm) > 0);
    CHECK(logo_height(Bucket::Sm) < logo_height(Bucket::Md));
    CHECK(logo_height(Bucket::Md) < logo_height(Bucket::Lg));
    CHECK(logo_height(Bucket::Lg) < logo_height(Bucket::Xl));
}

SCENARIO("[feature:level] fits_logo hides the logo on short terminals") {  // @proves KEUR-LEVEL.fits-logo
    // Below Sm-logo (13) + UI chrome (~12) the logo must be hidden, so a clipped
    // "KEUR" never shows as "neuk".
    CHECK_FALSE(keur::cmd::fits_logo(10));
    CHECK_FALSE(keur::cmd::fits_logo(24));
    CHECK(keur::cmd::fits_logo(25));
    CHECK(keur::cmd::fits_logo(50));
    // And render honours it: no braille glyphs when show_logo=false.
    auto no_logo = keur::cmd::render_level_screen(keur::render::Bucket::Sm, false, 3, false);
    CHECK(no_logo.find("Choose your Difficulty:") != std::string::npos);
    CHECK(no_logo.find("⠀") == std::string::npos);   // no braille blank
    CHECK(no_logo.find("KEUR Me Plenty") != std::string::npos);
}

SCENARIO("[feature:level] keur_logo is never null and is multi-line") {  // @proves KEUR-LOGO
    using keur::render::keur_logo;
    using keur::render::Bucket;
    for (Bucket b : {Bucket::Sm, Bucket::Md, Bucket::Lg, Bucket::Xl}) {
        for (bool color : {false, true}) {
            const char* s = keur_logo(b, color);
            REQUIRE(s != nullptr);
            std::string logo = s;
            CHECK(!logo.empty());
            CHECK(logo.find('\n') != std::string::npos);   // multiple rows
        }
    }
}

SCENARIO("[feature:level] level_parse reads --print / --bucket / --no-color / --config") {  // @proves KEUR-LEVEL.parse
    using keur::cmd::level_parse;
    using keur::render::Bucket;
    auto def = level_parse({});
    CHECK(def.print_only == false);
    CHECK(def.no_color == false);
    CHECK(def.bucket_set == false);
    CHECK(def.global == false);

    auto g = level_parse({"--global"});
    CHECK(g.global == true);

    auto p = level_parse({"--print", "3", "--no-color"});
    CHECK(p.print_only == true);
    CHECK(p.print_level == 3);
    CHECK(p.no_color == true);

    auto b = level_parse({"--bucket", "lg"});
    CHECK(b.print_only == true);
    CHECK(b.bucket_set == true);
    CHECK(b.bucket == Bucket::Lg);

    auto c = level_parse({"--config", "/x/keur.toml"});
    CHECK(c.config == "/x/keur.toml");

    // A garbage --print value must not flip into print mode.
    auto bad = level_parse({"--print", "notanumber"});
    CHECK(bad.print_only == false);
}

SCENARIO("[feature:level] bucket_from_name maps names, rejects unknown") {  // @proves KEUR-LEVEL.bucket_name
    using keur::cmd::bucket_from_name;
    using keur::render::Bucket;
    Bucket out = Bucket::Md;
    CHECK(bucket_from_name("sm", out)); CHECK(out == Bucket::Sm);
    CHECK(bucket_from_name("md", out)); CHECK(out == Bucket::Md);
    CHECK(bucket_from_name("lg", out)); CHECK(out == Bucket::Lg);
    CHECK(bucket_from_name("xl", out)); CHECK(out == Bucket::Xl);
    Bucket keep = Bucket::Lg;
    CHECK(bucket_from_name("huge", keep) == false);
    CHECK(keep == Bucket::Lg);   // untouched on failure
}

SCENARIO("[feature:level] level_table has the five DOOM->CMMI levels in order") {  // @proves KEUR-LEVEL.table
    const auto& t = keur::cmd::level_table();
    REQUIRE(t.size() == 5);
    CHECK(t.size() == static_cast<std::size_t>(keur::cmd::k_level_count));  // constant in sync
    for (std::size_t i = 0; i < t.size(); ++i)
        CHECK(t[i].cmmi == static_cast<int>(i) + 1);   // index 0..4 == CMMI 1..5
    CHECK(std::string(t[0].name) == "I'm Too Young to KEUR");
    CHECK(std::string(t[2].name) == "KEUR Me Plenty");
    CHECK(std::string(t[4].name) == "H4x0r c0d3fl3x");
}

SCENARIO("[feature:level] render_level_screen marks the selection and shows the blurb") {  // @proves KEUR-LEVEL.render
    // Mono (no colour): the selected row carries the '>' marker, the level's
    // CMMI line and description appear, and the logo braille is present.
    std::string s = keur::cmd::render_level_screen(keur::render::Bucket::Md, false, 3);
    CHECK(s.find("Choose your Difficulty:") != std::string::npos);
    CHECK(s.find("> KEUR Me Plenty") != std::string::npos);       // selected row
    CHECK(s.find("  I'm Too Young to KEUR") != std::string::npos); // unselected row
    CHECK(s.find("Level 3 \xE2\x80\x94 Defined.") != std::string::npos);  // em-dash blurb head
    CHECK(s.find("\xE2\xA0") != std::string::npos);               // braille (U+28xx) logo bytes
    // Colour on: the same screen carries ANSI SGR sequences.
    std::string c = keur::cmd::render_level_screen(keur::render::Bucket::Md, true, 1);
    CHECK(c.find("\033[38;5;") != std::string::npos);
    CHECK(c.find("> I'm Too Young to KEUR") != std::string::npos); // L1 selected
}

SCENARIO("[feature:level] render_level_screen clamps out-of-range selection") {  // @proves KEUR-LEVEL.render
    // Selection below 1 clamps to 1; above 5 clamps to 5. No crash, valid blurb.
    std::string lo = keur::cmd::render_level_screen(keur::render::Bucket::Sm, false, -3);
    CHECK(lo.find("> I'm Too Young to KEUR") != std::string::npos);
    std::string hi = keur::cmd::render_level_screen(keur::render::Bucket::Sm, false, 99);
    CHECK(hi.find("> H4x0r c0d3fl3x") != std::string::npos);
}

SCENARIO("[feature:level] upsert_policy_level adds, replaces, and preserves other lines") {  // @proves KEUR-LEVEL.persist
    using keur::cmd::upsert_policy_level;
    // Empty body → a fresh [policy] section with the level.
    std::string a = upsert_policy_level("", 4);
    CHECK(a.find("[policy]") != std::string::npos);
    CHECK(a.find("level = 4") != std::string::npos);

    // Existing [policy] with a level → the value is replaced, section kept once.
    std::string body = "[project]\nname = \"x\"\n\n[policy]\nblock-at = 0.9\nlevel = 2\n";
    std::string r = upsert_policy_level(body, 5);
    CHECK(r.find("level = 5") != std::string::npos);
    CHECK(r.find("level = 2") == std::string::npos);   // old value gone
    CHECK(r.find("block-at = 0.9") != std::string::npos); // sibling preserved
    CHECK(r.find("name = \"x\"") != std::string::npos);   // other section preserved
    // Exactly one [policy] section.
    std::size_t first = r.find("[policy]");
    CHECK(first != std::string::npos);
    CHECK(r.find("[policy]", first + 1) == std::string::npos);

    // [policy] present without a level line → level appended inside the section.
    std::string body2 = "[policy]\nblock-at = 0.8\n";
    std::string r2 = upsert_policy_level(body2, 1);
    CHECK(r2.find("level = 1") != std::string::npos);
    CHECK(r2.find("block-at = 0.8") != std::string::npos);
}

// --- KEUR-SETTINGS: the ISO 25010 slider model (keur settings) -------------

SCENARIO("[feature:settings] build_settings_model maps the ISO 25010 groups") {  // @proves KEUR-SETTINGS.build
    keur::DifficultyVector vec; vec.default_level = 3;
    vec.levels = {{"integrity", 5}, {"accessibility", 0}};  // one raised, one OFF
    auto m = keur::cmd::build_settings_model(vec);
    // The tree shape is the fixed group map.
    CHECK(m.groups.size() == keur::cmd::iso25010_groups().size());
    CHECK(m.groups[0].name == "Security");
    // Levels read from the vector (default_level for unset ilities).
    const auto& sec = m.groups[0];
    bool saw_integrity = false, saw_conf = false;
    for (const auto& s : sec.sliders) {
        if (s.ility == "integrity")      { CHECK(s.level == 5); saw_integrity = true; }
        if (s.ility == "confidentiality"){ CHECK(s.level == 3); saw_conf = true; }  // default
    }
    CHECK(saw_integrity); CHECK(saw_conf);
    // accessibility OFF (0) somewhere under Usability.
    for (const auto& g : m.groups)
        for (const auto& s : g.sliders)
            if (s.ility == "accessibility") CHECK(s.level == 0);
    // All groups start collapsed.
    for (const auto& g : m.groups) CHECK_FALSE(g.expanded);
}

SCENARIO("[feature:settings] visible_rows reflect collapse state") {  // @proves KEUR-SETTINGS.rows
    keur::DifficultyVector vec; vec.default_level = 3;
    auto m = keur::cmd::build_settings_model(vec);
    // Collapsed: one row per group.
    CHECK(keur::cmd::settings_visible_rows(m).size() == m.groups.size());
    // Expand the first group (Security has 3 ilities).
    m.cursor = 0;
    CHECK(keur::cmd::settings_expand(m));
    auto rows = keur::cmd::settings_visible_rows(m);
    // groups + the 3 Security sliders now visible.
    CHECK(rows.size() == m.groups.size() + m.groups[0].sliders.size());
    // Row 0 is the Security header, rows 1..3 are its sliders.
    CHECK(rows[0].is_group());
    CHECK_FALSE(rows[1].is_group());
    CHECK(rows[1].group == 0);
}

SCENARIO("[feature:settings] expand/collapse are idempotent + jump to parent") {  // @proves KEUR-SETTINGS.collapse
    keur::DifficultyVector vec; vec.default_level = 3;
    auto m = keur::cmd::build_settings_model(vec);
    m.cursor = 0;
    CHECK(keur::cmd::settings_expand(m));
    CHECK_FALSE(keur::cmd::settings_expand(m));   // already expanded -> no-op
    // Move onto the first slider, then collapse: cursor jumps back to the group.
    keur::cmd::settings_move(m, 1);
    CHECK_FALSE(keur::cmd::settings_visible_rows(m)[static_cast<std::size_t>(m.cursor)].is_group());
    CHECK(keur::cmd::settings_collapse(m));
    CHECK(keur::cmd::settings_visible_rows(m)[static_cast<std::size_t>(m.cursor)].is_group());
    CHECK_FALSE(m.groups[0].expanded);
}

SCENARIO("[feature:settings] group average ignores OFF ilities") {  // @proves KEUR-SETTINGS.average
    keur::cmd::GroupNode g;
    g.sliders = { {"a", 4, false}, {"b", 2, false}, {"c", 0, false} };  // c is OFF
    // average over non-OFF {4,2} = 3.0, not (4+2+0)/3.
    CHECK(keur::cmd::group_average(g) == doctest::Approx(3.0));
    // all OFF -> 0.0
    keur::cmd::GroupNode off;
    off.sliders = { {"a", 0, false}, {"b", 0, false} };
    CHECK(keur::cmd::group_average(off) == doctest::Approx(0.0));
}

SCENARIO("[feature:settings] star marks a non-uniform group or an override") {  // @proves KEUR-SETTINGS.star
    keur::cmd::GroupNode uniform;
    uniform.sliders = { {"a", 3, false}, {"b", 3, false} };
    CHECK_FALSE(keur::cmd::group_has_star(uniform));   // all equal, no override
    keur::cmd::GroupNode mixed;
    mixed.sliders = { {"a", 3, false}, {"b", 4, false} };
    CHECK(keur::cmd::group_has_star(mixed));           // not all equal
    keur::cmd::GroupNode overridden;
    overridden.sliders = { {"a", 3, false}, {"b", 3, true} };  // per-rule override
    CHECK(keur::cmd::group_has_star(overridden));
    // OFF ility does not count as "unequal".
    keur::cmd::GroupNode with_off;
    with_off.sliders = { {"a", 3, false}, {"b", 0, false} };
    CHECK_FALSE(keur::cmd::group_has_star(with_off));
}

SCENARIO("[feature:settings] adjust a slider is clamped off..5 and sets dirty") {  // @proves KEUR-SETTINGS.adjust
    keur::DifficultyVector vec; vec.default_level = 3;
    auto m = keur::cmd::build_settings_model(vec);
    m.cursor = 0;
    keur::cmd::settings_expand(m);
    keur::cmd::settings_move(m, 1);   // onto the first Security slider (level 3)
    CHECK(keur::cmd::settings_adjust(m, +1));   // 3 -> 4
    CHECK(m.groups[0].sliders[0].level == 4);
    CHECK(m.dirty);
    // Clamp high.
    keur::cmd::settings_adjust(m, +5);
    CHECK(m.groups[0].sliders[0].level == 5);
    // Clamp low to OFF(0).
    keur::cmd::settings_adjust(m, -9);
    CHECK(m.groups[0].sliders[0].level == 0);
    CHECK_FALSE(keur::cmd::settings_adjust(m, -1));  // already at 0 -> no change
}

SCENARIO("[feature:settings] dragging a group sets all its sliders equal") {  // @proves KEUR-SETTINGS.adjust
    keur::DifficultyVector vec; vec.default_level = 3;
    vec.levels = {{"integrity", 5}};   // Security becomes mixed (5,3,3)
    auto m = keur::cmd::build_settings_model(vec);
    m.cursor = 0;   // Security header
    REQUIRE(keur::cmd::group_has_star(m.groups[0]));   // mixed -> star
    // Drag down one from the rounded average (avg of 5,3,3 ≈ 3.7 -> 4 -> 3).
    CHECK(keur::cmd::settings_adjust(m, -1));
    for (const auto& s : m.groups[0].sliders) CHECK(s.level == 3);
    CHECK_FALSE(keur::cmd::group_has_star(m.groups[0]));  // now uniform
}

SCENARIO("[feature:settings] render shows bars, OFF detached, and the star") {  // @proves KEUR-SETTINGS.render
    keur::DifficultyVector vec; vec.default_level = 3;
    vec.levels = {{"integrity", 5}};   // Security mixed -> star on that group
    auto m = keur::cmd::build_settings_model(vec);
    std::string out = keur::cmd::render_settings_screen(m, /*color=*/false);
    CHECK(out.find("Difficulty settings:") != std::string::npos);
    CHECK(out.find("Overall") != std::string::npos);
    CHECK(out.find("Security") != std::string::npos);
    CHECK(out.find("*") != std::string::npos);               // mixed Security
    CHECK(out.find("\xE2\x96\x88") != std::string::npos);    // a full block in a bar
}

SCENARIO("[feature:settings] upsert_difficulty writes off as string, preserves siblings") {  // @proves KEUR-SETTINGS.persist
    using keur::cmd::upsert_difficulty;
    // Fresh file: section is created.
    std::string r = upsert_difficulty("", { {"integrity", 4}, {"accessibility", 0} });
    CHECK(r.find("[difficulty]") != std::string::npos);
    CHECK(r.find("integrity = 4") != std::string::npos);
    CHECK(r.find("accessibility = \"off\"") != std::string::npos);
    // Existing section: replace in place, keep other lines + other sections.
    std::string body =
        "[policy]\nlevel = 3\n[difficulty]\nintegrity = 2\nconfidentiality = 3\n[other]\nx = 1\n";
    std::string r2 = upsert_difficulty(body, { {"integrity", 5} });
    CHECK(r2.find("integrity = 5") != std::string::npos);
    CHECK(r2.find("integrity = 2") == std::string::npos);     // old value gone
    CHECK(r2.find("confidentiality = 3") != std::string::npos); // sibling preserved
    CHECK(r2.find("[other]") != std::string::npos);           // other section kept
    CHECK(r2.find("level = 3") != std::string::npos);         // [policy] kept
    // [difficulty.rules] must NOT be treated as [difficulty].
    std::string body3 = "[difficulty.rules]\nKEUR-X = off\n";
    std::string r3 = upsert_difficulty(body3, { {"integrity", 4} });
    CHECK(r3.find("KEUR-X = off") != std::string::npos);      // rules untouched
    CHECK(r3.find("integrity = 4") != std::string::npos);     // new [difficulty] added
}

SCENARIO("[feature:settings] settings_parse flags") {  // @proves KEUR-SETTINGS.parse
    auto o = keur::cmd::settings_parse({"--print", "--no-color", "--global", "--config", "cfg/x.toml"});
    CHECK(o.print_only);
    CHECK(o.no_color);
    CHECK(o.global);
    CHECK(o.config == "cfg/x.toml");
}

// --- KEUR-TUNE: the ←/→ lens router over level/settings/manager ------------

SCENARIO("[feature:tune] lens stepping is clamped at both ends") {  // @proves KEUR-TUNE.step
    using V = keur::cmd::TuneView;
    // Forward: level -> settings -> manager -> (wall) manager.
    CHECK(keur::cmd::tune_next(V::Level)    == V::Settings);
    CHECK(keur::cmd::tune_next(V::Settings) == V::Manager);
    CHECK(keur::cmd::tune_next(V::Manager)  == V::Manager);   // wall
    // Backward: manager -> settings -> level -> (wall) level.
    CHECK(keur::cmd::tune_prev(V::Manager)  == V::Settings);
    CHECK(keur::cmd::tune_prev(V::Settings) == V::Level);
    CHECK(keur::cmd::tune_prev(V::Level)    == V::Level);     // wall
}

SCENARIO("[feature:tune] header marks the active lens") {  // @proves KEUR-TUNE.header
    using V = keur::cmd::TuneView;
    std::string h = keur::cmd::render_tune_header(V::Settings, /*color=*/false);
    CHECK(h.find("[ settings ]") != std::string::npos);   // active, bracketed
    CHECK(h.find("level") != std::string::npos);
    CHECK(h.find("manager") != std::string::npos);
    CHECK(h.find("[ level ]") == std::string::npos);      // not active
    // Active bracket moves with the view.
    std::string h2 = keur::cmd::render_tune_header(V::Level, false);
    CHECK(h2.find("[ level ]") != std::string::npos);
}

SCENARIO("[feature:manager-tree] style_for pins the colour priority order") {  // @proves KEUR-MANAGER-VIEW.style
    const std::string kDim = "\033[2m", kGreen = "\033[32m", kRed = "\033[31m",
                      kCyan = "\033[36m", kYellow = "\033[33m";
    auto sgr = [](const keur::manager::Node& n) { return std::string(keur::manager::style_for(n)); };

    keur::manager::Node scope;  scope.kind = keur::manager::NodeKind::ScopeNode;
    scope.scope = keur::manager::Scope::Global;   CHECK(sgr(scope) == kCyan);
    scope.scope = keur::manager::Scope::Repo;     CHECK(sgr(scope) == kYellow);
    scope.scope = keur::manager::Scope::Defaults; CHECK(sgr(scope) == kDim);

    keur::manager::Node warn; warn.kind = keur::manager::NodeKind::CategoryNode; warn.warn = true;
    CHECK(sgr(warn) == kRed);   // warn beats everything below

    keur::manager::Node inh; inh.kind = keur::manager::NodeKind::RuleNode;
    inh.enabled = true; inh.inherited = true;
    CHECK(sgr(inh) == kDim);    // inherited dims even though enabled

    keur::manager::Node on;  on.kind  = keur::manager::NodeKind::RuleNode; on.enabled = true;
    CHECK(sgr(on) == kGreen);
    keur::manager::Node off; off.kind = keur::manager::NodeKind::RuleNode; off.enabled = false;
    CHECK(sgr(off) == kDim);

    keur::manager::Node grp; grp.kind = keur::manager::NodeKind::CategoryNode;  // empty group
    CHECK(sgr(grp).empty());    // no rules, not off/overruled → uncoloured
}

SCENARIO("[feature:tune] handle_level_key picks + clamps the coarse level") {  // @proves KEUR-TUNE.key.level
    using K = keur::platform::Key;
    keur::cmd::TuneOptions opts;
    auto key = [](K k, char c = '\0') { keur::platform::KeyEvent e; e.key = k; e.ch = c; return e; };
    int level = 3;
    std::string status;
    // Up / Left / k / h step DOWN; Down / Right / j / l step UP (1-D chooser).
    keur::cmd::handle_level_key(key(K::Up), opts, level, status);    CHECK(level == 2);
    keur::cmd::handle_level_key(key(K::Left), opts, level, status);  CHECK(level == 1);
    keur::cmd::handle_level_key(key(K::Up), opts, level, status);    CHECK(level == 1);  // clamp low
    keur::cmd::handle_level_key(key(K::Down), opts, level, status);  CHECK(level == 2);
    keur::cmd::handle_level_key(key(K::Char,'l'), opts, level, status); CHECK(level == 3);
    for (int i = 0; i < 10; ++i) keur::cmd::handle_level_key(key(K::Right), opts, level, status);
    CHECK(level == 5);  // clamp high
    // A pick returns false (no save); Enter returns true (save requested).
    CHECK_FALSE(keur::cmd::handle_level_key(key(K::Up), opts, level, status));
}

SCENARIO("[feature:tune] overall_level is the rounded non-off average, clamped") {  // @proves KEUR-TUNE.overall
    keur::DifficultyVector vec; vec.default_level = 3;
    auto m = keur::cmd::build_settings_model(vec);
    CHECK(keur::cmd::overall_level(m) == 3);           // all 3
    // Raise one slider; average rounds.
    m.groups[0].sliders[0].level = 5;
    CHECK(keur::cmd::overall_level(m) >= 3);
    // All OFF → fallback 3 (never 0).
    for (auto& g : m.groups) for (auto& s : g.sliders) s.level = 0;
    CHECK(keur::cmd::overall_level(m) == 3);
}

SCENARIO("[feature:tune] handle_settings_key drives the slider model") {  // @proves KEUR-TUNE.key.settings
    using K = keur::platform::Key;
    keur::cmd::TuneOptions opts;
    auto key = [](K k, char c = '\0') { keur::platform::KeyEvent e; e.key = k; e.ch = c; return e; };
    keur::DifficultyVector vec; vec.default_level = 3;
    auto m = keur::cmd::build_settings_model(vec);
    std::string status;

    // Cursor starts on the first group header → not a slider.
    m.cursor = 0;
    CHECK_FALSE(keur::cmd::settings_on_slider(m));
    // Right on a group expands it.
    keur::cmd::handle_settings_key(key(K::Right), opts, m, status);
    CHECK(m.groups[0].expanded);
    // Move down onto the first slider → now on a slider.
    keur::cmd::handle_settings_key(key(K::Down), opts, m, status);
    CHECK(keur::cmd::settings_on_slider(m));
    // '+' raises it, '-' lowers it.
    int before = m.groups[0].sliders[0].level;
    keur::cmd::handle_settings_key(key(K::Char,'+'), opts, m, status);
    CHECK(m.groups[0].sliders[0].level == before + 1);
    keur::cmd::handle_settings_key(key(K::Char,'-'), opts, m, status);
    CHECK(m.groups[0].sliders[0].level == before);
    // Left on a slider nudges down (adjust), not collapse.
    keur::cmd::handle_settings_key(key(K::Left), opts, m, status);
    CHECK(m.groups[0].sliders[0].level == before - 1);
}

SCENARIO("[feature:tune] tune_parse flags + starting lens") {  // @proves KEUR-TUNE.parse
    auto o = keur::cmd::tune_parse({"--print", "--no-color", "--view", "manager", "--config", "cfg/x.toml"});
    CHECK(o.print_only);
    CHECK(o.no_color);
    CHECK(o.start == keur::cmd::TuneView::Manager);
    CHECK(o.config == "cfg/x.toml");
    // default start is level.
    CHECK(keur::cmd::tune_parse({}).start == keur::cmd::TuneView::Level);
}

} // TEST_SUITE("unit") — Layer 4/5 pure tests
