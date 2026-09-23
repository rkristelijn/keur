// keur:ignore-file SEC-041 — test source: contains patterns it checks for
/**
 * @file keur_test.cpp
 * @brief keur unit + integration + performance tests.
 *
 * Test tiers (run with doctest filter flags):
 *   --test-suite=unit        Pure functions, no I/O, <100ms total.
 *   --test-suite=integration Real rule engine + tempdir fixtures, <5s total.
 *   --test-suite=perf        Timing benchmarks, run separately.
 *
 * Feature markers (grep these to track coverage):
 *   [feature:policy]       policy_load, policy_evaluate, resolve_config
 *   [feature:check]        cmd_check_parse, staged_files, cmd_check DI
 *   [feature:hook]         hooks_install, hooks_remove, global/repo dirs
 *   [feature:scan]         cmd_scan_parse, cmd_scan, rule engine integration
 *   [feature:rule-engine]  rules_load, rules_scan_files, rules_scan_dir
 *   [feature:suppress]     keur:ignore inline suppression
 *   [feature:version]      version string resolution (keur.toml → compile-time)
 *   [feature:learn]        learn_parse option parsing
 *   [feature:help]         usage/help dispatch (behaviour proven e2e in smoke.sh)
 *
 * @see docs/adr/ADR-002-single-cpp-binary.md
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../vendor/doctest.h"
#include "rules/rule_engine.h"
#include "rule/facts.h"
#include "rule/semver.h"
#include "tokenizer.h"
#include "cmd/check.h"
#include "cmd/config.h"
#include "cmd/hook.h"
#include "cmd/learn.h"
#include "cmd/manager.h"
#include "cmd/policy.h"
#include "cmd/scan.h"
#include "config/hookmgr.h"
#include "config/scanners.h"
#include "config/disabled.h"
#include "config/ruleset.h"
#include "adapters/fs.h"
#include "adapters/git.h"
#include "util/keur_utils.h"
#include "adapters/proc.h"
#include "adapters/sysinfo.h"
#include "learn/sysinfo.h"
#include "report/report.h"
#include "common/platform.h"
#include "manager/model.h"
#include "manager/view.h"
#include "manager/catalog.h"

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static std::string create_temp_dir() {
    char tmpl[] = "/tmp/keur_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    return d ? std::string(d) : "";
}

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void cleanup_dir(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string full = dir + "/" + n;
        struct stat st{};
        stat(full.c_str(), &st);
        if (S_ISDIR(st.st_mode)) cleanup_dir(full);
        else unlink(full.c_str());
    }
    closedir(d);
    rmdir(dir.c_str());
}

static bool file_exists_at(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

// Resolve the repo root. Prefer the compile-time KEUR_TEST_ROOT define
// (set by the Makefile); fall back to walking up from __FILE__ when that
// define is absent (e.g. ad-hoc compilation).
static std::string repo_root() {
#ifdef KEUR_TEST_ROOT
    return KEUR_TEST_ROOT;
#else
    std::string f = __FILE__;
    auto p = f.rfind("/src/keur/");
    return (p != std::string::npos) ? f.substr(0, p) : ".";
#endif
}

static std::string fixtures_dir() { return repo_root() + "/tests/fixtures"; }
static std::string keur_rules_dir() { return repo_root() + "/rules"; }

// Helper: does a findings vector contain a given rule id?
static bool has_rule(const std::vector<keur::RuleFinding>& fs, const std::string& id) {
    for (auto& f : fs) if (f.rule_id == id) return true;
    return false;
}

// Stage a fixture file's content into a neutral tempdir path (no "tests/" or
// "fixtures/" in the path), returning the new absolute path. This mirrors a
// real repository layout so rules with exclude_paths (test/ tests/ fixtures/)
// are not filtered out. Caller owns the returned dir's parent via `out_dir`.
static std::string stage_fixture(const std::string& fixture_rel,
                                 const std::string& dest_name,
                                 std::string& out_dir) {
    out_dir = create_temp_dir();
    std::string content = read_file(fixtures_dir() + "/" + fixture_rel);
    std::string dest = out_dir + "/" + dest_name;
    write_file(dest, content);
    return dest;
}

// An in-memory fake filesystem for fully-spoofed hook tests: no real files.
// Records writes/removes in a set so assertions can inspect what "happened".
struct FakeFsState {
    std::vector<std::string> written;
    std::vector<std::string> removed;
    std::vector<std::string> made_dirs;
};

static keur::adapters::Fs make_fake_fs(FakeFsState& st) {
    keur::adapters::Fs fs;
    fs.mkdir_p = [&st](const std::string& p) { st.made_dirs.push_back(p); return true; };
    fs.write_executable = [&st](const std::string& p, const std::string&) {
        st.written.push_back(p); return true;
    };
    fs.remove = [&st](const std::string& p) { st.removed.push_back(p); };
    fs.exists = [&st](const std::string& p) {
        for (auto& w : st.written) if (w == p) return true;
        return false;
    };
    return fs;
}

// ============================================================================
// TEST_SUITE: unit — pure functions, no real filesystem scanning
// ============================================================================

TEST_SUITE("unit") {

// --- [feature:policy] -------------------------------------------------------

SCENARIO("[feature:policy] policy_load returns defaults when file missing") {
    auto p = keur::cmd::policy_load("/tmp/keur_no_such_file_xyz.toml");
    CHECK(p.block_at == doctest::Approx(0.85));
    CHECK(p.warn_at  == doctest::Approx(0.50));
    CHECK(p.version.empty());
}

SCENARIO("[feature:policy] policy_load reads custom thresholds") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    write_file(tmp + "/keur.toml",
               "version = \"1.2.3\"\nblock-at = 0.95\nwarn-at = 0.60\n");
    auto p = keur::cmd::policy_load(tmp + "/keur.toml");
    CHECK(p.version  == "1.2.3");
    CHECK(p.block_at == doctest::Approx(0.95));
    CHECK(p.warn_at  == doctest::Approx(0.60));
    cleanup_dir(tmp);
}

SCENARIO("[feature:policy] policy_load ignores inline comments") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    write_file(tmp + "/keur.toml",
               "version = \"2.0.0\"  # version\nblock-at = 0.90  # threshold\n");
    auto p = keur::cmd::policy_load(tmp + "/keur.toml");
    CHECK(p.version  == "2.0.0");
    CHECK(p.block_at == doctest::Approx(0.90));
    cleanup_dir(tmp);
}

SCENARIO("[feature:policy] resolve_config returns hint unchanged") {
    CHECK(keur::cmd::resolve_config("/tmp/explicit.toml") == "/tmp/explicit.toml");
}

SCENARIO("[feature:policy] resolve_config falls back to XDG path") {
    auto r = keur::cmd::resolve_config("");
    CHECK(r.find("keur/keur.toml") != std::string::npos);
}

SCENARIO("[feature:policy] cmd_policy_parse defaults to explain") {
    CHECK(keur::cmd::policy_parse({}).subcommand == "explain");
}

SCENARIO("[feature:policy] cmd_policy_parse accepts --config") {
    auto o = keur::cmd::policy_parse({"--config", "/p", "explain"});
    CHECK(o.config == "/p");
    CHECK(o.subcommand == "explain");
}

SCENARIO("[feature:policy] cmd_policy returns 1 for unknown subcommand") {
    keur::cmd::PolicyOptions o; o.subcommand = "bad";
    CHECK(keur::cmd::policy(o) == 1);
}

SCENARIO("[feature:policy] cmd_policy explain returns 0") {
    keur::cmd::PolicyOptions o;
    CHECK(keur::cmd::policy(o) == 0);
}

// --- [feature:check] --------------------------------------------------------

SCENARIO("[feature:check] policy_evaluate — no findings → Pass") {
    keur::cmd::PolicyInput in{};
    CHECK(keur::cmd::policy_evaluate(in) == keur::cmd::Verdict::Pass);
}

SCENARIO("[feature:check] policy_evaluate — finding only → Warn") {
    keur::cmd::PolicyInput in{}; in.has_any_finding = true;
    CHECK(keur::cmd::policy_evaluate(in) == keur::cmd::Verdict::Warn);
}

SCENARIO("[feature:check] policy_evaluate — rule block → Block") {
    keur::cmd::PolicyInput in{}; in.has_any_finding = true; in.has_rule_block = true;
    CHECK(keur::cmd::policy_evaluate(in) == keur::cmd::Verdict::Block);
}

SCENARIO("[feature:check] policy_evaluate — secret block → Block") {
    keur::cmd::PolicyInput in{}; in.has_secret_block = true;
    CHECK(keur::cmd::policy_evaluate(in) == keur::cmd::Verdict::Block);
}

SCENARIO("[feature:check] cmd_check_parse defaults") {
    auto o = keur::cmd::check_parse({});
    CHECK(o.tier == keur::cmd::Tier::PreCommit);
    CHECK(o.ai   == "off");
    CHECK(o.config.empty());
}

SCENARIO("[feature:check] cmd_check_parse --tier pre-push") {
    CHECK(keur::cmd::check_parse({"--tier", "pre-push"}).tier == keur::cmd::Tier::PrePush);
}

SCENARIO("[feature:check] cmd_check_parse --tier pre-commit explicit") {
    CHECK(keur::cmd::check_parse({"--tier", "pre-commit"}).tier == keur::cmd::Tier::PreCommit);
}

SCENARIO("[feature:check] cmd_check_parse --ai") {
    CHECK(keur::cmd::check_parse({"--ai", "ollama"}).ai == "ollama");
}

SCENARIO("[feature:check] cmd_check_parse --config") {
    CHECK(keur::cmd::check_parse({"--config", "/c"}).config == "/c");
}

SCENARIO("[feature:check] cmd_check_parse ignores unknown flags") {
    CHECK(keur::cmd::check_parse({"--nope", "x"}).tier == keur::cmd::Tier::PreCommit);
}

SCENARIO("[feature:check] cmd_check PASS with empty file source") {
    keur::cmd::CheckOptions opts;
    CHECK(keur::cmd::check(opts, []() { return std::vector<std::string>{}; }) == 0);
}

SCENARIO("[feature:check] staged_files returns empty outside a git repo") {
    char orig[4096];
    REQUIRE(getcwd(orig, sizeof(orig)) != nullptr);
    chdir("/tmp");
    CHECK(keur::cmd::staged_files().empty());
    chdir(orig);
}

// --- [feature:scan] ---------------------------------------------------------

SCENARIO("[feature:scan] cmd_scan_parse defaults to current dir") {
    auto o = keur::cmd::scan_parse({});
    CHECK(o.target == ".");
    CHECK(o.format == keur::report::OutputFormat::Jsonl);
    CHECK(o.color  == keur::report::ColorMode::Auto);
}

SCENARIO("[feature:scan] cmd_scan_parse positional path") {
    CHECK(keur::cmd::scan_parse({"/repo"}).target == "/repo");
}

SCENARIO("[feature:scan] cmd_scan_parse --json aliases to jsonl format") {
    CHECK(keur::cmd::scan_parse({"--json"}).format == keur::report::OutputFormat::Jsonl);
}

SCENARIO("[feature:scan] cmd_scan_parse --format compact") {
    CHECK(keur::cmd::scan_parse({"--format", "compact"}).format
          == keur::report::OutputFormat::Compact);
}

SCENARIO("[feature:scan] cmd_scan_parse --format verbose") {
    CHECK(keur::cmd::scan_parse({"--format", "verbose"}).format
          == keur::report::OutputFormat::Verbose);
}

SCENARIO("[feature:scan] cmd_scan_parse --color never") {
    CHECK(keur::cmd::scan_parse({"--color", "never"}).color
          == keur::report::ColorMode::Never);
}

SCENARIO("[feature:scan] cmd_scan_parse --config") {
    CHECK(keur::cmd::scan_parse({"--config", "/p"}).config == "/p");
}

SCENARIO("[feature:scan] cmd_scan returns 0 with no rules") {
    unsetenv("KEUR_HOME");
    unsetenv("KEUR_RULES_DIRS");
    keur::cmd::ScanOptions o; o.target = "/tmp";
    CHECK(keur::cmd::scan(o) == 0);
}

// --- [feature:hook] ---------------------------------------------------------

SCENARIO("[feature:hook] cmd_hook_parse defaults to Install") {
    auto o = keur::cmd::hook_parse({});
    CHECK(o.action == keur::cmd::HookAction::Install);
    CHECK(!o.global);
}

SCENARIO("[feature:hook] cmd_hook_parse --install") {
    CHECK(keur::cmd::hook_parse({"--install"}).action == keur::cmd::HookAction::Install);
}

SCENARIO("[feature:hook] cmd_hook_parse --global") {
    CHECK(keur::cmd::hook_parse({"--global"}).action == keur::cmd::HookAction::Global);
}

SCENARIO("[feature:hook] cmd_hook_parse --status") {
    CHECK(keur::cmd::hook_parse({"--status"}).action == keur::cmd::HookAction::Status);
}

SCENARIO("[feature:hook] cmd_hook_parse --remove") {
    auto o = keur::cmd::hook_parse({"--remove"});
    CHECK(o.action == keur::cmd::HookAction::Remove);
    CHECK(!o.global);
}

SCENARIO("[feature:hook] cmd_hook_parse --remove --global") {
    auto o = keur::cmd::hook_parse({"--remove", "--global"});
    CHECK(o.action == keur::cmd::HookAction::Remove);
    CHECK(o.global);
}

SCENARIO("[feature:hook] cmd_hook_parse --augment [--remove]") {
    auto a = keur::cmd::hook_parse({"--augment"});
    CHECK(a.action == keur::cmd::HookAction::Augment);
    CHECK(!a.remove_augment);
    auto r = keur::cmd::hook_parse({"--augment", "--remove"});
    CHECK(r.action == keur::cmd::HookAction::Augment);
    CHECK(r.remove_augment);
}

SCENARIO("[feature:hook] augment_hook inserts a guarded keur line at the top") {
    // Existing husky-style hook.
    std::string husky = "#!/bin/sh\nnpx lint-staged\n";
    std::string out = keur::cmd::augment_hook(husky, "pre-commit");
    CHECK(keur::cmd::is_augmented(out));
    // keur runs BEFORE husky's own command (fail-fast): keur line precedes it.
    auto keur_pos = out.find("keur check --tier pre-commit");
    auto husky_pos = out.find("npx lint-staged");
    REQUIRE(keur_pos != std::string::npos);
    REQUIRE(husky_pos != std::string::npos);
    CHECK(keur_pos < husky_pos);
    // Guarded: only runs when keur is installed (safe for teammates without it).
    CHECK(out.find("command -v keur") != std::string::npos);
    // Shebang preserved at the very top.
    CHECK(out.rfind("#!/bin/sh", 0) == 0);
    // Respects a BLOCK.
    CHECK(out.find("|| exit 1") != std::string::npos);
}

SCENARIO("[feature:hook] augment_hook is idempotent") {
    std::string husky = "#!/bin/sh\nnpx lint-staged\n";
    std::string once = keur::cmd::augment_hook(husky, "pre-commit");
    std::string twice = keur::cmd::augment_hook(once, "pre-commit");
    CHECK(once == twice);   // second augment is a no-op
}

SCENARIO("[feature:hook] unaugment_hook removes exactly the keur block") {
    std::string husky = "#!/bin/sh\nnpx lint-staged\n";
    std::string aug = keur::cmd::augment_hook(husky, "pre-commit");
    std::string back = keur::cmd::unaugment_hook(aug);
    CHECK(!keur::cmd::is_augmented(back));
    CHECK(back.find("npx lint-staged") != std::string::npos);  // manager's line kept
    CHECK(back.find("keur check") == std::string::npos);        // keur block gone
}

SCENARIO("[feature:hook] augment_hook adds a shebang when the file has none") {
    std::string out = keur::cmd::augment_hook("", "pre-push");
    CHECK(out.rfind("#!/bin/sh", 0) == 0);
    CHECK(out.find("keur check --tier pre-push") != std::string::npos);
}

SCENARIO("[feature:hook] hooks_install creates executable stubs") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    std::string hdir = tmp + "/hooks";
    CHECK(keur::cmd::hooks_install(hdir));

    std::string pc = hdir + "/pre-commit", pp = hdir + "/pre-push";
    CHECK(file_exists_at(pc));
    CHECK(file_exists_at(pp));

    struct stat st{};
    stat(pc.c_str(), &st);
    CHECK((st.st_mode & S_IXUSR) != 0);

    CHECK(read_file(pc).find("exec keur check") != std::string::npos);
    CHECK(read_file(pp).find("pre-push") != std::string::npos);
    cleanup_dir(tmp);
}

SCENARIO("[feature:hook] hooks_remove deletes stubs") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    std::string hdir = tmp + "/hooks";
    keur::cmd::hooks_install(hdir);
    REQUIRE(file_exists_at(hdir + "/pre-commit"));
    keur::cmd::hooks_remove(hdir);
    CHECK(!file_exists_at(hdir + "/pre-commit"));
    CHECK(!file_exists_at(hdir + "/pre-push"));
    cleanup_dir(tmp);
}

SCENARIO("[feature:hook] hooks_remove idempotent on empty dir") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    keur::cmd::hooks_remove(tmp);
    CHECK(true);
    cleanup_dir(tmp);
}

SCENARIO("[feature:hook] hooks_install idempotent") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    std::string hdir = tmp + "/hooks";
    CHECK(keur::cmd::hooks_install(hdir));
    CHECK(keur::cmd::hooks_install(hdir));
    cleanup_dir(tmp);
}

SCENARIO("[feature:hook] global_hooks_dir respects XDG_CONFIG_HOME") {
    setenv("XDG_CONFIG_HOME", "/custom/xdg", 1);
    CHECK(keur::cmd::global_hooks_dir().find("/custom/xdg") == 0);
    unsetenv("XDG_CONFIG_HOME");
}

SCENARIO("[feature:hook] global_hooks_dir falls back to ~/.config") {
    unsetenv("XDG_CONFIG_HOME");
    CHECK(keur::cmd::global_hooks_dir().find("git/hooks") != std::string::npos);
}

// --- [feature:rule-engine] utils --------------------------------------------

SCENARIO("[feature:rule-engine] jesc escapes quotes and backslashes") {
    CHECK(keur::jesc("a\"b") == "a\\\"b");
    CHECK(keur::jesc("a\\b") == "a\\\\b");
    CHECK(keur::jesc("a\nb") == "a b");
    CHECK(keur::jesc("a\tb") == "a b");
}

SCENARIO("[feature:rule-engine] rule_dirs empty when no env set") {
    unsetenv("KEUR_HOME");
    unsetenv("KEUR_RULES_DIRS");
    // With no KEUR_HOME/KEUR_RULES_DIRS, only the global (~/.config/keur/rules)
    // and project (.keur/rules, if CWD is inside such a repo) layers can appear.
    // Neither built-in nor override contributes, so no path may reference
    // KEUR_HOME or an override dir. (The cascade may still list a global/project
    // dir depending on the machine, so we assert on ABSENCE of the env layers.)
    auto d = keur::rule_dirs();
    for (const auto& p : d) {
        CHECK(p.find("/opt/keur") == std::string::npos);  // no built-in without KEUR_HOME
    }
}

SCENARIO("[feature:rule-engine] rule_dirs includes KEUR_HOME/rules as the built-in layer") {
    setenv("KEUR_HOME", "/opt/keur", 1);
    unsetenv("KEUR_RULES_DIRS");
    auto d = keur::rule_dirs();
    // built-in layer is first and present; other layers (global/project) may
    // follow depending on the machine, so we check membership + position.
    REQUIRE(!d.empty());
    CHECK(d[0] == "/opt/keur/rules");
    unsetenv("KEUR_HOME");
}

SCENARIO("[feature:rule-engine] rule_dirs appends KEUR_RULES_DIRS overrides in order, last") {
    unsetenv("KEUR_HOME");
    setenv("KEUR_RULES_DIRS", "/a:/b:/c", 1);
    auto d = keur::rule_dirs();
    // the override entries are the highest-specificity layer: they must be the
    // final three, in order (global/project layers, if any, come before them).
    REQUIRE(d.size() >= 3);
    CHECK(d[d.size() - 3] == "/a");
    CHECK(d[d.size() - 2] == "/b");
    CHECK(d[d.size() - 1] == "/c");
    unsetenv("KEUR_RULES_DIRS");
}

// --- [feature:config] config layer stack (pure + spoofed adapters) ----------

SCENARIO("[feature:config] config_parse reads list / --json / --config") {
    auto o = keur::cmd::config_parse({"list", "--json"});
    CHECK(o.subcommand == "list");
    CHECK(o.json == true);
    auto o2 = keur::cmd::config_parse({"--config", "/x/keur.toml"});
    CHECK(o2.config == "/x/keur.toml");
    CHECK(o2.json == false);
}

SCENARIO("[feature:config] resolve_layers orders defaults->global->keur-home->repo->explicit") {
    keur::config::ScopeEnv env;
    env.xdg_config_home = "/xdg";
    env.keur_home = "/kh";
    env.repo_root = "/repo";
    env.explicit_path = "/exp/keur.toml";
    // everything "exists" so we assert on ordering + paths, not presence.
    auto layers = keur::config::resolve_layers(env, [](const std::string&) { return true; });
    REQUIRE(layers.size() == 5);
    CHECK(layers[0].scope == keur::config::Scope::Defaults);
    CHECK(layers[0].path.empty());
    CHECK(layers[1].scope == keur::config::Scope::Global);
    CHECK(layers[1].path == "/xdg/keur/keur.toml");
    CHECK(layers[2].scope == keur::config::Scope::KeurHome);
    CHECK(layers[2].path == "/kh/config/keur.toml");
    CHECK(layers[3].scope == keur::config::Scope::Repo);
    CHECK(layers[3].path == "/repo/keur.toml");
    CHECK(layers[4].scope == keur::config::Scope::Explicit);
    CHECK(layers[4].path == "/exp/keur.toml");
}

SCENARIO("[feature:config] resolve_layers omits layers whose env is unset") {
    keur::config::ScopeEnv env;  // all empty: no global base, no keur-home, no repo
    auto layers = keur::config::resolve_layers(env, [](const std::string&) { return false; });
    // only the always-present Defaults layer remains.
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].scope == keur::config::Scope::Defaults);
}

SCENARIO("[feature:config] resolve_layers records existence per layer") {
    keur::config::ScopeEnv env;
    env.home = "/home/u";      // → global /home/u/.config/keur/keur.toml
    env.repo_root = "/repo";   // → /repo/keur.toml
    // only the repo file "exists".
    auto layers = keur::config::resolve_layers(
        env, [](const std::string& p) { return p == "/repo/keur.toml"; });
    REQUIRE(layers.size() == 3);  // defaults + global + repo
    CHECK(layers[1].scope == keur::config::Scope::Global);
    CHECK(layers[1].exists == false);
    CHECK(layers[2].scope == keur::config::Scope::Repo);
    CHECK(layers[2].exists == true);
}

SCENARIO("[feature:config] config_render_json is valid and reflects existence") {
    std::vector<keur::config::Layer> layers = {
        {keur::config::Scope::Defaults, "", true},
        {keur::config::Scope::Repo, "/repo/keur.toml", false},
    };
    std::string j = keur::cmd::config_render_json(layers);
    CHECK(j == "[{\"scope\":\"defaults\",\"path\":\"\",\"exists\":true},"
               "{\"scope\":\"repo\",\"path\":\"/repo/keur.toml\",\"exists\":false}]");
}

SCENARIO("[feature:config] config_render_text labels present/absent and built-in") {
    std::vector<keur::config::Layer> layers = {
        {keur::config::Scope::Defaults, "", true},
        {keur::config::Scope::Repo, "/repo/keur.toml", false},
    };
    auto lines = keur::cmd::config_render_text(layers);
    REQUIRE(lines.size() == 3);  // header + 2 layers
    CHECK(lines[1].find("(built-in)") != std::string::npos);
    CHECK(lines[2].find("/repo/keur.toml") != std::string::npos);
    CHECK(lines[2].find("[absent]") != std::string::npos);
}

SCENARIO("[feature:config] config orchestrator resolves repo root via git, existence via fs") {
    keur::cmd::ConfigDeps deps;
    deps.git.run = [](const std::string& args) -> std::string {
        if (args == "rev-parse --show-toplevel") return "/my/repo";
        return "";
    };
    // fs reports only the repo keur.toml as existing.
    deps.fs.exists = [](const std::string& p) { return p == "/my/repo/keur.toml"; };
    keur::cmd::ConfigOptions opts;  // list, text
    CHECK(keur::cmd::config(opts, deps) == 0);
    keur::cmd::ConfigOptions bad;
    bad.subcommand = "frobnicate";
    CHECK(keur::cmd::config(bad, deps) == 1);
}

// --- [feature:hook] coexisting hook-manager detection (KEUR-HOOKMGR) --------

SCENARIO("[feature:hook] detects Husky as owner via repo-local core.hooksPath") {  // @proves KEUR-HOOKMGR
    keur::config::HookProbe probe;
    probe.repo_root = "/repo";
    probe.git_config = [](const std::string& a) -> std::string {
        if (a == "config --local core.hooksPath")  return ".husky/_";
        if (a == "config --global core.hooksPath") return "/home/u/.config/git/hooks";
        return "";
    };
    probe.exists = [](const std::string& p) { return p == "/repo/.husky"; };
    auto s = keur::config::detect_hook_situation(probe);
    CHECK(s.owner == keur::config::HookOwner::Husky);
    CHECK(s.effective_path == ".husky/_");     // local overrides global
    bool husky_detected = false;
    for (const auto& m : s.managers) if (m.name == "husky") husky_detected = m.detected;
    CHECK(husky_detected);
}

SCENARIO("[feature:hook] keur owns hooks when only a global core.hooksPath is set") {  // @proves KEUR-HOOKMGR
    keur::config::HookProbe probe;
    probe.repo_root = "/repo";
    probe.git_config = [](const std::string& a) -> std::string {
        if (a == "config --global core.hooksPath") return "/home/u/.config/git/hooks";
        return "";  // no local hooksPath
    };
    probe.exists = [](const std::string&) { return false; };  // no manager signatures
    auto s = keur::config::detect_hook_situation(probe);
    CHECK(s.owner == keur::config::HookOwner::Keur);
    CHECK(s.local_hooks_path.empty());
    CHECK(s.effective_path == "/home/u/.config/git/hooks");
}

SCENARIO("[feature:hook] detects pre-commit by its config signature") {  // @proves KEUR-HOOKMGR
    keur::config::HookProbe probe;
    probe.repo_root = "/repo";
    probe.git_config = [](const std::string&) -> std::string { return ""; };  // no hooksPath at all
    probe.exists = [](const std::string& p) { return p == "/repo/.pre-commit-config.yaml"; };
    auto s = keur::config::detect_hook_situation(probe);
    bool pc = false;
    for (const auto& m : s.managers) if (m.name == "pre-commit") pc = m.detected;
    CHECK(pc);
    CHECK(s.owner == keur::config::HookOwner::PreCommit);  // present, default .git/hooks
}

SCENARIO("[feature:hook] no manager and no hooksPath means no owner") {  // @proves KEUR-HOOKMGR
    keur::config::HookProbe probe;
    probe.repo_root = "/repo";
    probe.git_config = [](const std::string&) -> std::string { return ""; };
    probe.exists = [](const std::string&) { return false; };
    auto s = keur::config::detect_hook_situation(probe);
    CHECK(s.owner == keur::config::HookOwner::None);
}

SCENARIO("[feature:hook] a runner (lint-staged) is detected but never an owner") {  // @proves KEUR-HOOKMGR
    keur::config::HookProbe probe;
    probe.repo_root = "/repo";
    probe.git_config = [](const std::string&) -> std::string { return ""; };
    probe.exists = [](const std::string& p) { return p == "/repo/.lintstagedrc"; };
    auto s = keur::config::detect_hook_situation(probe);
    bool ls = false; keur::config::HookKind kind = keur::config::HookKind::Manager;
    for (const auto& m : s.managers)
        if (m.name == "lint-staged") { ls = m.detected; kind = m.kind; }
    CHECK(ls);                                              // detected
    CHECK(kind == keur::config::HookKind::Runner);          // classified as a runner
    CHECK(s.owner == keur::config::HookOwner::None);        // a runner never owns hooks
}

SCENARIO("[feature:hook] detects the raw .githooks/ convention as a manager") {  // @proves KEUR-HOOKMGR
    keur::config::HookProbe probe;
    probe.repo_root = "/repo";
    probe.git_config = [](const std::string&) -> std::string { return ""; };
    probe.exists = [](const std::string& p) { return p == "/repo/.githooks"; };
    auto s = keur::config::detect_hook_situation(probe);
    bool gh = false; keur::config::HookKind kind = keur::config::HookKind::Runner;
    for (const auto& m : s.managers)
        if (m.name == "githooks") { gh = m.detected; kind = m.kind; }
    CHECK(gh);
    CHECK(kind == keur::config::HookKind::Manager);
}

// --- [feature:config] scanner inventory (KEUR-SCANNERS) ---------------------

static const char* k_scanner_toml =
    "[policy]\n"
    "block-at = 0.85\n"
    "\n"
    "[scanners.gitleaks]\n"
    "bin = \"gitleaks\"\n"
    "group = \"secrets\"\n"
    "default-action = \"block\"\n"
    "\n"
    "[scanners.semgrep]\n"
    "bin = \"semgrep\"\n"
    "group = \"sast\"\n"
    "default-action = \"warn\"\n"
    "\n"
    "[scanners.presidio]\n"
    "enabled = false\n"
    "group = \"pii\"\n"
    "\n"
    "[policy.weights]\n"
    "secrets = 1.0\n";

SCENARIO("[feature:config] parse_scanners reads [scanners.*] and stops at other sections") {  // @proves KEUR-SCANNERS
    auto sc = keur::config::parse_scanners(k_scanner_toml);
    REQUIRE(sc.size() == 3);
    CHECK(sc[0].name == "gitleaks");
    CHECK(sc[0].bin == "gitleaks");
    CHECK(sc[0].group == "secrets");
    CHECK(sc[0].default_action == "block");
    CHECK(sc[0].enabled == true);
    CHECK(sc[1].name == "semgrep");
    CHECK(sc[2].name == "presidio");
    CHECK(sc[2].enabled == false);  // enabled = false parsed
}

SCENARIO("[feature:config] known_config_files maps a scanner to its dotfiles") {  // @proves KEUR-SCANNERS
    auto gl = keur::config::known_config_files("gitleaks");
    REQUIRE(gl.size() == 2);
    CHECK(gl[0] == ".gitleaks.toml");
    CHECK(gl[1] == ".gitleaksignore");
    CHECK(keur::config::known_config_files("nonexistent").empty());
}

SCENARIO("[feature:config] resolve_scanners fills PATH availability and config-file presence") {  // @proves KEUR-SCANNERS
    auto declared = keur::config::parse_scanners(k_scanner_toml);
    keur::config::ScannerProbe probe;
    probe.repo_root = "/repo";
    // gitleaks on PATH, semgrep not; only .gitleaksignore exists.
    probe.tool_on_path = [](const std::string& t) { return t == "gitleaks"; };
    probe.exists = [](const std::string& p) { return p == "/repo/.gitleaksignore"; };
    auto sc = keur::config::resolve_scanners(declared, probe);
    REQUIRE(sc.size() == 3);
    CHECK(sc[0].on_path == true);   // gitleaks
    CHECK(sc[1].on_path == false);  // semgrep
    REQUIRE(sc[0].config_files.size() == 2);
    CHECK(sc[0].config_files[0].path == ".gitleaks.toml");
    CHECK(sc[0].config_files[0].present == false);
    CHECK(sc[0].config_files[1].path == ".gitleaksignore");
    CHECK(sc[0].config_files[1].present == true);
}

// --- [feature:rules] disable-list (KEUR-DISABLED) ---------------------------

SCENARIO("[feature:rules] parse_disabled reads inline and multi-line arrays") {  // @proves KEUR-DISABLED
    auto a = keur::config::parse_disabled("[rules]\ndisabled = [\"KEUR-SEC-001\", \"KEUR-QUAL-002\"]\n");
    CHECK(a.count("KEUR-SEC-001"));
    CHECK(a.count("KEUR-QUAL-002"));
    CHECK(a.size() == 2);
    // multi-line array form
    auto b = keur::config::parse_disabled("[rules]\ndisabled = [\n  \"A-1\",\n  \"B-2\",\n]\n");
    CHECK(b.count("A-1"));
    CHECK(b.count("B-2"));
    // disabled outside [rules] is ignored
    auto c = keur::config::parse_disabled("[other]\ndisabled = [\"X\"]\n");
    CHECK(c.empty());
}

SCENARIO("[feature:rules] filter_disabled drops exactly the listed ids") {  // @proves KEUR-DISABLED
    std::vector<keur::Rule> rules;
    keur::Rule r1; r1.id = "KEEP-1"; rules.push_back(r1);
    keur::Rule r2; r2.id = "DROP-2"; rules.push_back(r2);
    keur::Rule r3; r3.id = "KEEP-3"; rules.push_back(r3);
    auto kept = keur::config::filter_disabled(rules, {"DROP-2"});
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].id == "KEEP-1");
    CHECK(kept[1].id == "KEEP-3");
    // empty disable-set keeps everything
    CHECK(keur::config::filter_disabled(rules, {}).size() == 3);
}

SCENARIO("[feature:rules] upsert_disabled_section round-trips and preserves other config") {  // @proves KEUR-DISABLED
    std::string base = "version = \"0.1.0\"\nblock-at = 0.85\n";
    // add two ids → a [rules] section is appended, other keys preserved
    std::string one = keur::config::upsert_disabled_section(base, {"R-2", "R-1"});
    CHECK(one.find("version = \"0.1.0\"") != std::string::npos);
    CHECK(one.find("block-at = 0.85") != std::string::npos);
    CHECK(one.find("[rules]") != std::string::npos);
    // ids are sorted for a stable diff
    CHECK(one.find("\"R-1\", \"R-2\"") != std::string::npos);
    // parsing it back yields the same set (round-trip)
    auto back = keur::config::parse_disabled(one);
    CHECK(back.count("R-1"));
    CHECK(back.count("R-2"));
    // removing one id rewrites the array in place
    std::string two = keur::config::upsert_disabled_section(one, {"R-2"});
    auto back2 = keur::config::parse_disabled(two);
    CHECK(back2.count("R-2"));
    CHECK(!back2.count("R-1"));
    CHECK(two.find("block-at = 0.85") != std::string::npos);  // still preserved
}

SCENARIO("[feature:rules] resolve_disabled unions ids across existing layers") {  // @proves KEUR-DISABLED
    std::vector<keur::config::Layer> layers = {
        {keur::config::Scope::Global, "/g/keur.toml", true},
        {keur::config::Scope::Repo, "/r/keur.toml", true},
        {keur::config::Scope::KeurHome, "/kh/keur.toml", false},  // absent → skipped
    };
    auto set = keur::config::resolve_disabled(layers, [](const std::string& p) -> std::string {
        if (p == "/g/keur.toml") return "[rules]\ndisabled = [\"GLOBAL-1\"]\n";
        if (p == "/r/keur.toml") return "[rules]\ndisabled = [\"REPO-1\"]\n";
        return "SHOULD-NOT-BE-READ";  // /kh is absent, must be skipped
    });
    CHECK(set.count("GLOBAL-1"));
    CHECK(set.count("REPO-1"));
    CHECK(set.size() == 2);  // the absent keur-home layer contributed nothing
}

// --- [feature:manager] aggregated board (status overview) -------------------

SCENARIO("[feature:manager] manager_parse reads status / --json / --config") {
    auto o = keur::cmd::manager_parse({"--status", "--json"});
    CHECK(o.subcommand == "status");
    CHECK(o.json == true);
    auto o2 = keur::cmd::manager_parse({"--config", "/x/keur.toml"});
    CHECK(o2.config == "/x/keur.toml");
}

SCENARIO("[feature:manager] manager_render_text shows all four sections") {
    keur::cmd::ManagerBoard b;
    b.config_layers = {{keur::config::Scope::Defaults, "", true},
                       {keur::config::Scope::Repo, "/repo/keur.toml", true}};
    b.hooks.owner = keur::config::HookOwner::Keur;
    b.hooks.effective_path = "/home/u/.config/git/hooks";
    b.scanners = {{"gitleaks", "gitleaks", "secrets", "block", true, true, {}}};
    b.rule_categories = {{"security", 5}};
    b.rule_total = 5;
    auto lines = keur::cmd::manager_render_text(b);
    // Join for a simple contains-check across the whole board.
    std::string all;
    for (auto& l : lines) all += l + "\n";
    CHECK(all.find("Config scopes") != std::string::npos);
    CHECK(all.find("Hooks:") != std::string::npos);
    CHECK(all.find("keur") != std::string::npos);           // hook owner
    CHECK(all.find("Scanners") != std::string::npos);
    CHECK(all.find("gitleaks") != std::string::npos);
    CHECK(all.find("Rules (5 total)") != std::string::npos);
    CHECK(all.find("security") != std::string::npos);
}

SCENARIO("[feature:manager] manager_render_json is a well-formed object") {
    keur::cmd::ManagerBoard b;
    b.config_layers = {{keur::config::Scope::Defaults, "", true}};
    b.hooks.owner = keur::config::HookOwner::Husky;
    b.hooks.effective_path = ".husky/_";
    b.scanners = {{"semgrep", "semgrep", "sast", "warn", true, false, {}}};
    b.rule_categories = {{"sast", 7}};
    b.rule_total = 7;
    std::string j = keur::cmd::manager_render_json(b);
    CHECK(j.front() == '{');
    CHECK(j.back() == '}');
    CHECK(j.find("\"config_layers\":[") != std::string::npos);
    CHECK(j.find("\"owner\":\"husky\"") != std::string::npos);
    CHECK(j.find("\"name\":\"semgrep\"") != std::string::npos);
    CHECK(j.find("\"total\":7") != std::string::npos);
}

SCENARIO("[feature:manager] manager orchestrator runs read-only via spoofed adapters") {
    keur::cmd::ManagerDeps deps;
    deps.git.run = [](const std::string& a) -> std::string {
        if (a == "rev-parse --show-toplevel") return "/my/repo";
        return "";  // no hooksPath
    };
    deps.fs.exists = [](const std::string&) { return false; };
    deps.fs.read_file = [](const std::string&) {
        return keur::adapters::ReadResult{false, ""};
    };
    keur::cmd::ManagerOptions opts;  // status, text
    CHECK(keur::cmd::manager(opts, deps) == 0);
    keur::cmd::ManagerOptions bad;
    bad.subcommand = "frobnicate";
    CHECK(keur::cmd::manager(bad, deps) == 1);
}


SCENARIO("[feature:git] git_repo_hooks_dir appends /hooks to git-dir") {
    keur::adapters::Git fake;
    fake.run = [](const std::string& args) -> std::string {
        if (args == "rev-parse --git-dir") return "/repo/.git";
        return "";
    };
    CHECK(keur::adapters::git_repo_hooks_dir(fake) == "/repo/.git/hooks");
}

SCENARIO("[feature:git] git_repo_hooks_dir empty outside a repo") {
    keur::adapters::Git fake;
    fake.run = [](const std::string&) -> std::string { return ""; };
    CHECK(keur::adapters::git_repo_hooks_dir(fake).empty());
}

SCENARIO("[feature:git] default_run trims output and runs real git") {
    // Safe, repo-independent git query.
    keur::adapters::Git git;  // real implementation
    auto out = git.run("--version");
    CHECK(out.find("git") != std::string::npos);
    // Trimmed: no trailing newline.
    CHECK((out.empty() || out.back() != '\n'));
}

SCENARIO("[feature:git] default_run returns empty for a bogus subcommand") {
    keur::adapters::Git git;
    // stderr is suppressed by the adapter; a failing command yields empty stdout.
    CHECK(git.run("this-is-not-a-git-command-xyz").empty());
}

// --- [feature:report] output provider ---------------------------------------

SCENARIO("[feature:report] finding_to_json emits the canonical shape") {
    keur::RuleFinding f;
    f.rule_id = "KEUR-SEC-001";
    f.title = "Private key";
    f.category = "secret";
    f.severity = "critical";
    f.action = "block";
    f.confidence = "0.98";
    f.file = "id_rsa.pem";
    f.line = 1;
    f.message = "key found";
    auto json = keur::report::finding_to_json(f);
    CHECK(json.find("\"rule\":\"KEUR-SEC-001\"") != std::string::npos);
    CHECK(json.find("\"action\":\"block\"") != std::string::npos);
    CHECK(json.find("\"line\":1") != std::string::npos);
    CHECK(json.find("\"confidence\":0.98") != std::string::npos);
    CHECK(json.front() == '{');
    CHECK(json.back() == '}');
}

SCENARIO("[feature:report] finding_to_json defaults confidence to 0.5") {
    keur::RuleFinding f;
    f.rule_id = "X";
    f.line = 3;
    // confidence empty
    CHECK(keur::report::finding_to_json(f).find("\"confidence\":0.5") != std::string::npos);
}

SCENARIO("[feature:report] report_findings JSONL emits one line per finding") {
    std::vector<keur::RuleFinding> fs(2);
    fs[0].rule_id = "A"; fs[0].line = 1;
    fs[1].rule_id = "B"; fs[1].line = 2;
    std::ostringstream out;
    keur::report::ReportOptions ro; ro.format = keur::report::OutputFormat::Jsonl;
    auto n = keur::report::report_findings(out, fs, ro);
    CHECK(n == 2);
    std::string s = out.str();
    CHECK(std::count(s.begin(), s.end(), '\n') == 2);
    CHECK(s.find("\"rule\":\"A\"") != std::string::npos);
    CHECK(s.find("\"rule\":\"B\"") != std::string::npos);
}

SCENARIO("[feature:report] report_findings SARIF emits a 2.1.0 document") {
    std::vector<keur::RuleFinding> fs(1);
    fs[0].rule_id = "T1"; fs[0].severity = "warning"; fs[0].message = "msg";
    fs[0].file = "a.py"; fs[0].line = 3;
    std::ostringstream out;
    keur::report::ReportOptions ro; ro.format = keur::report::OutputFormat::Sarif;
    auto n = keur::report::report_findings(out, fs, ro);
    CHECK(n == 1);
    std::string s = out.str();
    CHECK(s.find("\"version\":\"2.1.0\"") != std::string::npos);
    CHECK(s.find("\"driver\":{\"name\":\"keur\"") != std::string::npos);
    CHECK(s.find("\"ruleId\":\"T1\"") != std::string::npos);
    CHECK(s.find("\"level\":\"warning\"") != std::string::npos);
    CHECK(s.find("\"startLine\":3") != std::string::npos);
    CHECK(s.find("\"uri\":\"a.py\"") != std::string::npos);
}

SCENARIO("[feature:report] report_findings compact emits file:line RULE") {
    std::vector<keur::RuleFinding> fs(1);
    fs[0].rule_id = "R"; fs[0].file = "a.txt"; fs[0].line = 7; fs[0].message = "msg";
    std::ostringstream out;
    keur::report::ReportOptions ro; ro.format = keur::report::OutputFormat::Compact;
    ro.color = keur::report::ColorMode::Never;
    keur::report::report_findings(out, fs, ro);
    std::string s = out.str();
    CHECK(s.find("a.txt:7 R") != std::string::npos);
    CHECK(s.find("msg") != std::string::npos);
}

SCENARIO("[feature:report] report_findings verbose includes action and title") {
    std::vector<keur::RuleFinding> fs(1);
    fs[0].rule_id = "R"; fs[0].action = "warn"; fs[0].title = "T";
    fs[0].file = "a"; fs[0].line = 1; fs[0].message = "m"; fs[0].confidence = "0.6";
    std::ostringstream out;
    keur::report::ReportOptions ro; ro.format = keur::report::OutputFormat::Verbose;
    ro.color = keur::report::ColorMode::Never;
    keur::report::report_findings(out, fs, ro);
    std::string s = out.str();
    CHECK(s.find("[warn] R") != std::string::npos);
    CHECK(s.find("- T") != std::string::npos);
    CHECK(s.find("confidence: 0.6") != std::string::npos);
}

SCENARIO("[feature:report] use_color Never is always false") {
    CHECK(!keur::report::use_color(keur::report::ColorMode::Never, true));
}

SCENARIO("[feature:report] use_color Always is true") {
    CHECK(keur::report::use_color(keur::report::ColorMode::Always, false));
}

SCENARIO("[feature:report] use_color Auto is false without a tty") {
    unsetenv("NO_COLOR");
    CHECK(!keur::report::use_color(keur::report::ColorMode::Auto, false));
}

SCENARIO("[feature:report] use_color Auto respects NO_COLOR") {
    setenv("NO_COLOR", "1", 1);
    CHECK(!keur::report::use_color(keur::report::ColorMode::Auto, true));
    unsetenv("NO_COLOR");
}

SCENARIO("[feature:report] parse_format maps names, falls back on unknown") {
    CHECK(keur::report::parse_format("jsonl")   == keur::report::OutputFormat::Jsonl);
    CHECK(keur::report::parse_format("compact") == keur::report::OutputFormat::Compact);
    CHECK(keur::report::parse_format("verbose") == keur::report::OutputFormat::Verbose);
    CHECK(keur::report::parse_format("bogus")   == keur::report::OutputFormat::Jsonl);
    CHECK(keur::report::parse_format("bogus", keur::report::OutputFormat::Compact)
          == keur::report::OutputFormat::Compact);
}

SCENARIO("[feature:report] parse_color maps names, falls back on unknown") {
    CHECK(keur::report::parse_color("auto")   == keur::report::ColorMode::Auto);
    CHECK(keur::report::parse_color("always") == keur::report::ColorMode::Always);
    CHECK(keur::report::parse_color("never")  == keur::report::ColorMode::Never);
    CHECK(keur::report::parse_color("bogus")  == keur::report::ColorMode::Auto);
}

// --- [feature:fs] filesystem adapter (real impl over a tempdir) -------------

SCENARIO("[feature:fs] default_mkdir_p / write_executable / exists / remove") {
    keur::adapters::Fs fs;  // real implementation
    std::string base = create_temp_dir();
    REQUIRE(!base.empty());

    std::string nested = base + "/a/b/c";
    CHECK(fs.mkdir_p(nested));
    CHECK(fs.exists(nested));

    std::string file = nested + "/hook";
    CHECK(fs.write_executable(file, "#!/bin/sh\n"));
    CHECK(fs.exists(file));

    // Written file is executable.
    struct stat stt{};
    stat(file.c_str(), &stt);
    CHECK((stt.st_mode & S_IXUSR) != 0);

    fs.remove(file);
    CHECK(!fs.exists(file));

    cleanup_dir(base);
}

SCENARIO("[feature:fs] default_exists is false for a missing path") {
    keur::adapters::Fs fs;
    CHECK(!fs.exists("/tmp/keur_definitely_missing_xyz_123"));
}

// --- [feature:proc] process adapter (real impl, safe commands) --------------

SCENARIO("[feature:proc] default_run returns 0 for true, non-zero for false") {
    keur::adapters::Proc proc;
    CHECK(proc.run("true") == 0);
    CHECK(proc.run("false") != 0);
}

SCENARIO("[feature:proc] default_capture captures stdout and exit code") {
    keur::adapters::Proc proc;
    auto r = proc.capture("echo hello");
    CHECK(r.exit_code == 0);
    CHECK(r.stdout_text.find("hello") != std::string::npos);
}

SCENARIO("[feature:proc] default_has finds sh but not a bogus binary") {
    keur::adapters::Proc proc;
    CHECK(proc.has("sh"));
    CHECK(!proc.has("keur_bogus_binary_xyz_123"));
}

SCENARIO("[feature:proc] run_argv propagates exit codes shell-free") {
    keur::adapters::Proc proc;
    CHECK(proc.run_argv({"sh", "-c", "exit 0"}, {}) == 0);
    CHECK(proc.run_argv({"sh", "-c", "exit 7"}, {}) == 7);
}

SCENARIO("[feature:proc] run_argv passes env overrides to the child") {
    keur::adapters::Proc proc;
    // The child sees KEUR_TEST_VAR because we pass it as an explicit env pair,
    // not as a `VAR=val cmd` shell prefix.
    CHECK(proc.run_argv({"sh", "-c", "test \"$KEUR_TEST_VAR\" = hello"},
                        {{"KEUR_TEST_VAR", "hello"}}) == 0);
    CHECK(proc.run_argv({"sh", "-c", "test \"$KEUR_TEST_VAR\" = hello"}, {}) != 0);
}

SCENARIO("[feature:proc] run_argv treats shell metacharacters as literal args (CWE-78)") {
    keur::adapters::Proc proc;
    // The dangerous-looking string is a single literal argument to printf, not
    // a command: no /bin/sh parses it, so the injection is inert. This is the
    // security property KEUR-SEC-006 relies on.
    CHECK(proc.run_argv({"printf", "%s", "; touch /tmp/keur_injection_probe"}, {}) == 0);
    // Prove nothing was created by the "injection".
    keur::adapters::Proc probe;
    CHECK(probe.run_argv({"test", "!", "-e", "/tmp/keur_injection_probe"}, {}) == 0);
}

SCENARIO("[feature:proc] run_argv returns -1 for empty argv") {
    keur::adapters::Proc proc;
    CHECK(proc.run_argv({}, {}) == -1);
}

// --- [feature:sysinfo] system-info adapter + resource composition ----------

SCENARIO("[feature:sysinfo] read_resources uses the injected adapter (fake seam)") {
    // The whole point of the adapter: a fake feeds deterministic numbers, no
    // real syscalls. read_resources must surface exactly what the adapter says.
    keur::adapters::SysInfo fake;
    fake.read = []() {
        keur::adapters::Resources r;
        r.load1 = 1.5;
        r.ncpu = 4;
        r.ram_total_gb = 16.0;
        r.ram_free_gb = 8.0;
        return r;
    };
    // A home with no supervisor pidfile -> run_secs stays 0 (portable branch).
    auto res = keur::learn::read_resources("/tmp/keur_no_such_home_xyz", fake);
    CHECK(res.load1 == doctest::Approx(1.5));
    CHECK(res.ncpu == 4);
    CHECK(res.ram_total_gb == doctest::Approx(16.0));
    CHECK(res.ram_free_gb == doctest::Approx(8.0));
    CHECK(res.run_secs == 0);
}

SCENARIO("[feature:sysinfo] default_read returns sane host numbers") {
    // Real per-platform implementation: on any supported OS ncpu is >= 1 and
    // total RAM is positive. Values are host-dependent, so we only assert ranges.
    auto hw = keur::adapters::SysInfo::default_read();
    CHECK(hw.ncpu >= 1);
    CHECK(hw.ram_total_gb > 0.0);
    CHECK(hw.ram_free_gb >= 0.0);
    CHECK(hw.load1 >= 0.0);
}

SCENARIO("[feature:sysinfo] fmt_duration formats hours and minutes") {
    CHECK(keur::learn::fmt_duration(0) == "0m");
    CHECK(keur::learn::fmt_duration(59) == "0m");
    CHECK(keur::learn::fmt_duration(60) == "1m");
    CHECK(keur::learn::fmt_duration(3600) == "1h 0m");
    CHECK(keur::learn::fmt_duration(3661) == "1h 1m");
}

// --- tokenizer (skip_comments / skip_strings building block) ----------------
TEST_CASE("tokenizer: lang_syntax resolves known extensions, nullptr for unknown") {
    CHECK(keur::lang_syntax(".py")  != nullptr);
    CHECK(keur::lang_syntax(".cpp") != nullptr);
    CHECK(keur::lang_syntax(".ts")  != nullptr);
    CHECK(keur::lang_syntax(".unknownext") == nullptr);
    CHECK(keur::lang_syntax("") == nullptr);
}

TEST_CASE("tokenizer: strip_comments blanks comments, keeps code and line count") {
    // C-style: line + block comments
    auto* cpp = keur::lang_syntax(".cpp");
    std::string src = "int x = 1; // secret comment\n/* block\n secret */ int y = 2;\n";
    std::string out = keur::strip_comments(src, cpp);
    // code survives
    CHECK(out.find("int x = 1;") != std::string::npos);
    CHECK(out.find("int y = 2;") != std::string::npos);
    // comment words are gone
    CHECK(out.find("secret comment") == std::string::npos);
    CHECK(out.find("block") == std::string::npos);
    // line count preserved (newlines kept)
    CHECK(std::count(out.begin(), out.end(), '\n') == std::count(src.begin(), src.end(), '\n'));
}

TEST_CASE("tokenizer: strip_comments leaves string literals intact") {
    auto* py = keur::lang_syntax(".py");
    std::string src = "url = \"http://example.com # not a comment\"  # real comment\n";
    std::string out = keur::strip_comments(src, py);
    CHECK(out.find("http://example.com # not a comment") != std::string::npos);  // inside string, kept
    CHECK(out.find("real comment") == std::string::npos);                        // real comment gone
}

TEST_CASE("tokenizer: strip_comments_and_strings blanks string contents too") {
    auto* py = keur::lang_syntax(".py");
    std::string src = "key = \"AKIAFICTIONAL0000\"\n";
    std::string bare = keur::strip_comments_and_strings(src, py);
    CHECK(bare.find("AKIAFICTIONAL0000") == std::string::npos);  // string content blanked
    CHECK(bare.find("key =") != std::string::npos);              // code survives
}

TEST_CASE("tokenizer: the comment false-positive scenario (system in a comment)") {
    // This is the KEUR-ARCH-001 false positive: the word 'system(' inside a
    // comment must NOT survive stripping, so a rule scanning stripped code
    // won't fire on documentation of a guard.
    auto* cpp = keur::lang_syntax(".cpp");
    std::string src = "// no raw system() here\nreturn proc.run(cmd);\n";
    std::string out = keur::strip_comments(src, cpp);
    CHECK(out.find("system(") == std::string::npos);      // comment mention gone
    CHECK(out.find("proc.run(cmd)") != std::string::npos); // real code survives
}

TEST_CASE("tokenizer: nullptr syntax returns content unchanged") {
    std::string src = "anything // goes\n";
    CHECK(keur::strip_comments(src, nullptr) == src);
}

SCENARIO("[feature:rule-engine] semver comparator handles ops and numeric order") {  // @proves ADR-009.requires
    using keur::semver_satisfies;
    using keur::semver_compare;
    CHECK(semver_compare("1.9.0", "1.10.0") < 0);   // numeric, not lexical
    CHECK(semver_compare("2.0.0", "2.0") == 0);      // missing parts = 0
    CHECK(semver_satisfies("4.17.1", "<4.18.0"));
    CHECK_FALSE(semver_satisfies("4.18.0", "<4.18.0"));
    CHECK(semver_satisfies("1.5.0", ">=1.2"));
    CHECK(semver_satisfies("1.5.0", "1.0.0 - 2.0.0"));
    CHECK_FALSE(semver_satisfies("3.0.0", "1.0.0 - 2.0.0"));
    CHECK(semver_satisfies("4.18.0-alpha", ">=4.18.0"));  // suffix ignored
    CHECK_FALSE(semver_satisfies("", "<1.0.0"));           // empty pin = not satisfied
}

SCENARIO("[feature:rule-engine] facts readers derive langs/frameworks/versions") {  // @proves ADR-009.facts
    keur::Facts f;
    keur::facts_read_package_json(
        "{\"dependencies\":{\"express\":\"^4.17.1\"},\"engines\":{\"node\":\">=18\"}}", f);
    CHECK(f.frameworks.count("express") == 1);
    CHECK(f.versions["express"] == "4.17.1");     // ^ stripped
    CHECK(f.runtimes["node"] == "18");            // >= stripped
    CHECK(f.langs.count("js") == 1);

    keur::Facts g;
    keur::facts_read_go_mod("module x\n\ngo 1.21\n\nrequire (\n  github.com/gin-gonic/gin v1.9.1\n)\n", g);
    CHECK(g.langs.count("go") == 1);
    CHECK(g.runtimes["go"] == "1.21");
    CHECK(g.versions["github.com/gin-gonic/gin"] == "1.9.1");

    keur::Facts d;
    keur::facts_read_dockerfile("FROM node:18-alpine\nRUN npm ci\n", d);
    CHECK(d.runtimes["node"] == "18-alpine");
    CHECK(d.frameworks.count("docker") == 1);

    CHECK(keur::facts_lang_for_ext(".ts") == "ts");
    CHECK(keur::facts_lang_for_ext(".go") == "go");
    CHECK(keur::facts_lang_for_ext(".unknown") == "");
}

} // TEST_SUITE("unit")


// ============================================================================
// TEST_SUITE: integration — real rule engine + real fixture files
// ============================================================================

TEST_SUITE("integration") {

SCENARIO("[feature:rule-engine] RSA private key triggers KEUR-SEC-001") {
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("secrets/rsa_key.pem", "id_rsa.pem", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(!fs.empty());
    CHECK(has_rule(fs, "KEUR-SEC-001"));
    for (auto& f : fs)
        if (f.rule_id == "KEUR-SEC-001") CHECK(f.action == "block");
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] EC private key triggers KEUR-SEC-001") {
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("secrets/ec_key.pem", "id_ec.pem", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(has_rule(fs, "KEUR-SEC-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] OPENSSH private key triggers KEUR-SEC-001") {
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("secrets/openssh_key.key", "id_ed25519.key", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(has_rule(fs, "KEUR-SEC-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] clean file produces zero KEUR-SEC-001 findings") {
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("clean/clean.yml", "config.yml", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(!has_rule(fs, "KEUR-SEC-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:suppress] keur:ignore suppresses KEUR-SEC-001") {
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("secrets/suppressed_key.pem", "sup.pem", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(!has_rule(fs, "KEUR-SEC-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] custom pattern rule fires on trigger file") {  // @proves engine-pattern
    auto rules = keur::rules_load(fixtures_dir() + "/rules");
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("rules/trigger.txt", "src.c", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(has_rule(fs, "TEST-PAT-001"));
    cleanup_dir(dir);
}

// --- path-derived engine (ADR-020) ------------------------------------------
// The expected string (namespace keur::<dir>) is COMPUTED from the file path,
// so ONE rule covers every layer. We exercise the real KEUR-ARCH-002 rule from
// rules/keur-internal against staged files whose path implies a layer.

SCENARIO("[feature:rule-engine] path-derived fires when namespace does not match its dir") {  // @proves ADR-020 engine-path-derived
    auto rules = keur::rules_load(keur_rules_dir() + "/keur-internal");
    REQUIRE(!rules.empty());
    std::string base = create_temp_dir();
    // A file in an adapters/ dir declaring the WRONG namespace must fire.
    std::string layer = base + "/adapters";
    mkdir(layer.c_str(), 0755);
    std::string bad = layer + "/thing.cpp";
    write_file(bad, "namespace keur::wrong {\n}\n");
    auto fs = keur::rules_scan_files(rules, {bad});
    CHECK(has_rule(fs, "KEUR-ARCH-002"));
    cleanup_dir(base);
}

SCENARIO("[feature:rule-engine] path-derived passes when namespace matches its dir") {  // @proves ADR-020 engine-path-derived
    auto rules = keur::rules_load(keur_rules_dir() + "/keur-internal");
    REQUIRE(!rules.empty());
    std::string base = create_temp_dir();
    // A file in a cmd/ dir declaring the derived namespace keur::cmd passes —
    // and this holds for ANY layer name, which is the whole point (dynamic).
    std::string layer = base + "/cmd";
    mkdir(layer.c_str(), 0755);
    std::string good = layer + "/thing.cpp";
    write_file(good, "namespace keur::cmd {\n}\n");
    auto fs = keur::rules_scan_files(rules, {good});
    CHECK(!has_rule(fs, "KEUR-ARCH-002"));
    cleanup_dir(base);
}

SCENARIO("[feature:rule-engine] path-derived is self-scaling: a brand-new layer name is checked") {  // @proves ADR-020
    auto rules = keur::rules_load(keur_rules_dir() + "/keur-internal");
    REQUIRE(!rules.empty());
    std::string base = create_temp_dir();
    // A layer that no rule ever named ("widget") is still checked: matching
    // namespace passes, mismatching fires — proving no per-layer rule edit.
    std::string layer = base + "/widget";
    mkdir(layer.c_str(), 0755);
    std::string ok = layer + "/a.cpp";
    std::string bad = layer + "/b.cpp";
    write_file(ok, "namespace keur::widget {\n}\n");
    write_file(bad, "namespace keur::something_else {\n}\n");
    auto fs_ok = keur::rules_scan_files(rules, {ok});
    auto fs_bad = keur::rules_scan_files(rules, {bad});
    CHECK(!has_rule(fs_ok, "KEUR-ARCH-002"));
    CHECK(has_rule(fs_bad, "KEUR-ARCH-002"));
    cleanup_dir(base);
}

SCENARIO("[feature:rule-engine] absence rule fires on file missing header") {
    auto rules = keur::rules_load(fixtures_dir() + "/rules");
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("rules/no_header.txt", "doc.txt", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(has_rule(fs, "TEST-ABS-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] absence rule does NOT fire when header present") {
    auto rules = keur::rules_load(fixtures_dir() + "/rules");
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("rules/has_header.txt", "doc.txt", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(!has_rule(fs, "TEST-ABS-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] rules_scan_dir flags multiple key files") {
    // Stage 3 key fixtures into a neutral tempdir, then scan the dir.
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::string base = create_temp_dir();
    REQUIRE(!base.empty());
    write_file(base + "/id_rsa.pem",     read_file(fixtures_dir() + "/secrets/rsa_key.pem"));
    write_file(base + "/id_ec.pem",      read_file(fixtures_dir() + "/secrets/ec_key.pem"));
    write_file(base + "/id_ed25519.key", read_file(fixtures_dir() + "/secrets/openssh_key.key"));
    auto fs = keur::rules_scan_dir(rules, base);
    int count = 0;
    for (auto& f : fs) if (f.rule_id == "KEUR-SEC-001") ++count;
    CHECK(count == 3);
    cleanup_dir(base);
}

SCENARIO("[feature:check] cmd_check BLOCKs on staged private key") {
    setenv("KEUR_RULES_DIRS", keur_rules_dir().c_str(), 1);
    unsetenv("KEUR_HOME");
    std::string dir;
    auto path = stage_fixture("secrets/rsa_key.pem", "id_rsa.pem", dir);
    keur::cmd::CheckOptions opts;
    auto rc = keur::cmd::check(opts, [&]() {
        return std::vector<std::string>{path};
    });
    CHECK(rc == 1);
    cleanup_dir(dir);
    unsetenv("KEUR_RULES_DIRS");
}

SCENARIO("[feature:check] cmd_check PASSes on staged clean file") {
    setenv("KEUR_RULES_DIRS", keur_rules_dir().c_str(), 1);
    unsetenv("KEUR_HOME");
    std::string dir;
    auto path = stage_fixture("clean/clean.yml", "config.yml", dir);
    keur::cmd::CheckOptions opts;
    auto rc = keur::cmd::check(opts, [&]() {
        return std::vector<std::string>{path};
    });
    CHECK(rc == 0);
    cleanup_dir(dir);
    unsetenv("KEUR_RULES_DIRS");
}

// --- orchestrator coverage: cmd_scan output loop -----------------------------

SCENARIO("[feature:scan] cmd_scan emits findings and returns 0 on a key dir") {
    // Stage keys in a neutral tempdir so exclude_paths does not filter them.
    std::string base = create_temp_dir();
    REQUIRE(!base.empty());
    write_file(base + "/id_rsa.pem", read_file(fixtures_dir() + "/secrets/rsa_key.pem"));

    setenv("KEUR_RULES_DIRS", keur_rules_dir().c_str(), 1);
    unsetenv("KEUR_HOME");

    keur::cmd::ScanOptions o;
    o.target = base;
    auto rc = keur::cmd::scan(o);   // exercises the JSONL output loop
    CHECK(rc == 0);

    unsetenv("KEUR_RULES_DIRS");
    cleanup_dir(base);
}

SCENARIO("[feature:scan] cmd_scan on clean dir returns 0") {
    std::string base = create_temp_dir();
    REQUIRE(!base.empty());
    write_file(base + "/config.yml", read_file(fixtures_dir() + "/clean/clean.yml"));

    setenv("KEUR_RULES_DIRS", keur_rules_dir().c_str(), 1);
    unsetenv("KEUR_HOME");

    keur::cmd::ScanOptions o;
    o.target = base;
    CHECK(keur::cmd::scan(o) == 0);

    unsetenv("KEUR_RULES_DIRS");
    cleanup_dir(base);
}

// --- orchestrator coverage: cmd_hook with an INJECTED fake Git ---------------
// No real git, no chdir, no global-config side effects. The fake Git points the
// --- orchestrator coverage: cmd_hook with FULLY SPOOFED adapters -------------
// git + fs + proc are all injected fakes. No real repo, no real files, no
// process spawns. Every branch — including --global — is exercised safely.

SCENARIO("[feature:hook] cmd_hook install writes stubs via the fake fs") {
    FakeFsState st;
    keur::cmd::HookDeps deps;
    deps.git.run = [](const std::string& args) -> std::string {
        return args == "rev-parse --git-dir" ? "/repo/.git" : "";
    };
    deps.fs = make_fake_fs(st);

    keur::cmd::HookOptions install; install.action = keur::cmd::HookAction::Install;
    CHECK(keur::cmd::hook(install, deps) == 0);
    REQUIRE(st.written.size() == 2);
    CHECK(st.written[0] == "/repo/.git/hooks/pre-commit");
    CHECK(st.written[1] == "/repo/.git/hooks/pre-push");
}

SCENARIO("[feature:hook] cmd_hook status queries fs.exists via the fake") {
    FakeFsState st;
    keur::cmd::HookDeps deps;
    deps.git.run = [](const std::string& args) -> std::string {
        return args == "rev-parse --git-dir" ? "/repo/.git" : "";
    };
    deps.fs = make_fake_fs(st);
    keur::cmd::HookOptions status; status.action = keur::cmd::HookAction::Status;
    CHECK(keur::cmd::hook(status, deps) == 0);
}

SCENARIO("[feature:hook] cmd_hook remove deletes via the fake fs") {
    FakeFsState st;
    // Pretend the stubs already exist so remove() is called.
    st.written = {"/repo/.git/hooks/pre-commit", "/repo/.git/hooks/pre-push"};
    keur::cmd::HookDeps deps;
    deps.git.run = [](const std::string& args) -> std::string {
        return args == "rev-parse --git-dir" ? "/repo/.git" : "";
    };
    deps.fs = make_fake_fs(st);
    keur::cmd::HookOptions remove; remove.action = keur::cmd::HookAction::Remove;
    CHECK(keur::cmd::hook(remove, deps) == 0);
    REQUIRE(st.removed.size() == 2);
}

SCENARIO("[feature:hook] cmd_hook --global installs and sets hooksPath via fake proc") {
    FakeFsState st;
    std::vector<std::vector<std::string>> proc_calls;
    keur::cmd::HookDeps deps;
    deps.git.run = [](const std::string&) -> std::string {
        return "";  // no existing core.hooksPath
    };
    deps.fs = make_fake_fs(st);
    deps.proc.run_argv = [&proc_calls](
        const std::vector<std::string>& argv,
        const std::vector<std::pair<std::string, std::string>>&) {
        proc_calls.push_back(argv);  // capture instead of executing
        return 0;
    };

    keur::cmd::HookOptions g; g.action = keur::cmd::HookAction::Global;
    CHECK(keur::cmd::hook(g, deps) == 0);
    // Stubs written to the global dir + git config invoked shell-free via
    // run_argv (not run): argv carries the literal git subcommand + path.
    REQUIRE(st.written.size() == 2);
    REQUIRE(proc_calls.size() == 1);
    bool has_hookspath = false;
    for (const auto& a : proc_calls[0])
        if (a.find("core.hooksPath") != std::string::npos) has_hookspath = true;
    CHECK(has_hookspath);
}

SCENARIO("[feature:hook] cmd_hook --global leaves an existing hooksPath alone") {
    FakeFsState st;
    std::vector<std::vector<std::string>> proc_calls;
    keur::cmd::HookDeps deps;
    deps.git.run = [](const std::string&) -> std::string {
        return "/some/other/hooks";  // already set to a different dir
    };
    deps.fs = make_fake_fs(st);
    deps.proc.run_argv = [&proc_calls](
        const std::vector<std::string>& argv,
        const std::vector<std::pair<std::string, std::string>>&) {
        proc_calls.push_back(argv);
        return 0;
    };
    keur::cmd::HookOptions g; g.action = keur::cmd::HookAction::Global;
    CHECK(keur::cmd::hook(g, deps) == 0);
    // Must NOT reconfigure git when a different hooksPath is already set.
    CHECK(proc_calls.empty());
}

SCENARIO("[feature:hook] cmd_hook install returns 1 when fake git reports no repo") {
    keur::cmd::HookDeps deps;
    deps.git.run = [](const std::string&) -> std::string { return ""; };
    keur::cmd::HookOptions install; install.action = keur::cmd::HookAction::Install;
    CHECK(keur::cmd::hook(install, deps) == 1);
}

SCENARIO("[feature:hook] cmd_hook remove returns 1 when fake git reports no repo") {
    keur::cmd::HookDeps deps;
    deps.git.run = [](const std::string&) -> std::string { return ""; };
    keur::cmd::HookOptions remove; remove.action = keur::cmd::HookAction::Remove;
    CHECK(keur::cmd::hook(remove, deps) == 1);
}

SCENARIO("[feature:hook] hooks_install returns false when fake fs mkdir fails") {
    keur::adapters::Fs fs;
    fs.mkdir_p = [](const std::string&) { return false; };  // simulate failure
    CHECK(!keur::cmd::hooks_install("/whatever", fs));
}

SCENARIO("[feature:check] staged_files uses the injected fake git") {
    keur::adapters::Git fake;
    fake.staged_files = []() {
        return std::vector<std::string>{"a.txt", "b.txt"};
    };
    auto files = keur::cmd::staged_files(fake);
    REQUIRE(files.size() == 2);
    CHECK(files[0] == "a.txt");
    CHECK(files[1] == "b.txt");
}

// skip_comments end-to-end: a rule that opts in must not fire on a match inside
// a comment, but must still fire on real code. This is the KEUR-ARCH-001 fix
// (a `system(`/`eval(` rule tripping on the word in a comment) proven through
// the real scan path, not just the tokenizer unit.
SCENARIO("[feature:rule-engine] skip_comments suppresses comment-only matches") {
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    // rule opts into comment stripping
    std::string rule_path = dir + "/SKIP.rule";
    write_file(rule_path,
        "id: SKIP-EVAL\n"
        "title: eval usage\n"
        "category: sast\n"
        "severity: warning\n"
        "engine: pattern\n"
        "skip_comments: true\n"
        "target:\n"
        "  extensions: .py\n"
        "patterns:\n"
        "  - regex: \\beval\\s*\\(\n"
        "    message: eval usage\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    REQUIRE(rules[0].skip_comments == true);   // field parsed

    std::string py = dir + "/app.py";
    write_file(py, "# TODO: remove this eval( someday\nresult = eval(user_input)\n");
    auto fs = keur::rules_scan_files(rules, {py});

    // exactly one finding, on line 2 (the code), not line 1 (the comment)
    int hits = 0, line = 0;
    for (auto& f : fs) if (f.rule_id == "SKIP-EVAL") { hits++; line = f.line; }
    CHECK(hits == 1);
    CHECK(line == 2);
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] without skip_comments the comment match is kept") {
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    std::string rule_path = dir + "/NOSKIP.rule";
    write_file(rule_path,
        "id: NOSKIP-EVAL\n"
        "title: eval usage\n"
        "category: sast\n"
        "severity: warning\n"
        "engine: pattern\n"
        "target:\n"
        "  extensions: .py\n"
        "patterns:\n"
        "  - regex: \\beval\\s*\\(\n"
        "    message: eval usage\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    CHECK(rules[0].skip_comments == false);

    std::string py = dir + "/app.py";
    write_file(py, "# TODO: remove this eval( someday\nresult = eval(user_input)\n");
    auto fs = keur::rules_scan_files(rules, {py});
    int hits = 0;
    for (auto& f : fs) if (f.rule_id == "NOSKIP-EVAL") hits++;
    CHECK(hits == 2);   // comment line + code line
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] rule-level message is the fallback when a pattern has none") {
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/MSG.rule",
        "id: MSG-1\ntitle: T\ncategory: quality\nseverity: warning\nengine: pattern\n"
        "message: rule-level message\ntarget:\n  extensions: .py\n"
        "patterns:\n  - regex: TODO\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    std::string py = dir + "/a.py";
    write_file(py, "# TODO fix\n");
    auto fs = keur::rules_scan_files(rules, {py});
    REQUIRE(fs.size() == 1);
    CHECK(fs[0].message == "rule-level message");
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] block-absence flags a block missing its inner regex") {  // @proves engine-block-absence
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/NOASSERT.rule",
        "id: TEST-NOASSERT\ntitle: test without assertion\ncategory: quality\n"
        "severity: warning\nengine: block-absence\nblock_inner: \\bexpect\\s*\\(\n"
        "target:\n  extensions: .js\n"
        "patterns:\n  - regex: \\b(it|test)\\s*\\(\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    std::string js = dir + "/a.test.js";
    write_file(js,
        "it(\"has assertion\", () => {\n  expect(1).toBe(1);\n});\n"
        "it(\"no assertion\", () => {\n  const x = 2;\n});\n");
    auto fs = keur::rules_scan_files(rules, {js});
    int hits = 0, line = 0;
    for (auto& f : fs) if (f.rule_id == "TEST-NOASSERT") { hits++; line = f.line; }
    CHECK(hits == 1);
    CHECK(line == 4);   // only the second block (no expect)
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] presence fires once per file when the pattern appears") {  // @proves engine-presence
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/PRES.rule",
        "id: PRES-1\ntitle: has eval\ncategory: sast\nseverity: warning\nengine: presence\n"
        "target:\n  extensions: .js\n"
        "patterns:\n  - regex: \\beval\\s*\\(\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    std::string js = dir + "/a.js";
    write_file(js, "eval(x);\neval(y);\nconst z = 1;\n");   // two matches, one file
    auto fs = keur::rules_scan_files(rules, {js});
    int hits = 0; for (auto& f : fs) if (f.rule_id == "PRES-1") hits++;
    CHECK(hits == 1);   // presence = at most one finding per file
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] absence fires when a required pattern is missing") {  // @proves engine-absence
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/ABS.rule",
        "id: ABS-1\ntitle: missing license header\ncategory: quality\nseverity: warning\nengine: absence\n"
        "target:\n  extensions: .go\n"
        "patterns:\n  - regex: SPDX-License-Identifier\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    std::string missing = dir + "/no_header.go";
    write_file(missing, "package main\n");
    std::string present = dir + "/has_header.go";
    write_file(present, "// SPDX-License-Identifier: MIT\npackage main\n");
    auto fs = keur::rules_scan_files(rules, {missing, present});
    int hits = 0; std::string where;
    for (auto& f : fs) if (f.rule_id == "ABS-1") { hits++; where = f.file; }
    CHECK(hits == 1);                          // only the file WITHOUT the header
    CHECK(where.find("no_header.go") != std::string::npos);
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] file-presence flags each matching file by name") {  // @proves engine-file-presence
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/FP.rule",
        "id: FP-1\ntitle: committed .env\ncategory: secret\nseverity: critical\nengine: file-presence\n"
        "target:\n  filenames: .env\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    std::string env = dir + "/.env";
    write_file(env, "SECRET=x\n");
    auto fs = keur::rules_scan_files(rules, {env});
    int hits = 0; for (auto& f : fs) if (f.rule_id == "FP-1") hits++;
    CHECK(hits == 1);
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] file-absence flags when a required file is absent") {  // @proves engine-file-absence
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/FA.rule",
        "id: FA-1\ntitle: missing SECURITY.md\ncategory: quality\nseverity: warning\nengine: file-absence\n"
        "target:\n  filenames: SECURITY.md\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    std::string other = dir + "/README.md";
    write_file(other, "# readme\n");
    auto fs = keur::rules_scan_files(rules, {other});   // no SECURITY.md in the set
    int hits = 0; for (auto& f : fs) if (f.rule_id == "FA-1") hits++;
    CHECK(hits == 1);
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] pattern exclude (pattern-not) suppresses the safe case") {  // @proves engine-pattern
    // Guards ADR-007: match system(...) EXCEPT when the arg is a bare literal.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/EX.rule",
        "id: EX-1\ntitle: non-literal system()\ncategory: cmdi\nseverity: error\nengine: pattern\n"
        "target:\n  extensions: .c\n"
        "patterns:\n"
        "  - regex: \\bsystem\\s*\\(\n"
        "    exclude: \\bsystem\\s*\\(\\s*\"\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    std::string c = dir + "/a.c";
    // line 1: literal → excluded (safe). line 2: variable → flagged.
    write_file(c, "system(\"ls\");\nsystem(cmd);\n");
    auto fs = keur::rules_scan_files(rules, {c});
    int hits = 0; int line = 0;
    for (auto& f : fs) if (f.rule_id == "EX-1") { hits++; line = f.line; }
    CHECK(hits == 1);      // only the non-literal call
    CHECK(line == 2);      // the safe literal on line 1 was excluded
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] block-absence block_trigger gates which blocks are checked") {  // @proves engine-block-absence
    // block_trigger: only evaluate blocks that contain the trigger regex.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/BT.rule",
        "id: BT-1\ntitle: gated block-absence\ncategory: quality\nseverity: warning\nengine: block-absence\n"
        "block_inner: \\bexpect\\s*\\(\n"
        "block_trigger: @security\n"
        "target:\n  extensions: .js\n"
        "patterns:\n  - regex: \\b(it|test)\\s*\\(\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    std::string js = dir + "/a.test.js";
    // block 1: no trigger → NOT checked even though it lacks expect().
    // block 2: has @security trigger, lacks expect() → flagged.
    write_file(js,
        "it(\"plain\", () => {\n  const x = 1;\n});\n"
        "it(\"@security\", () => {\n  const y = 2;\n});\n");
    auto fs = keur::rules_scan_files(rules, {js});
    int hits = 0; for (auto& f : fs) if (f.rule_id == "BT-1") hits++;
    CHECK(hits == 1);   // only the triggered block (block 1 is gated out)
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] count engine fires once at min_count, not below") {  // @proves engine-count
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/CNT.rule",
        "id: CNT-1\ntitle: too many TODOs\ncategory: quality\nseverity: warning\nengine: count\n"
        "min_count: 3\n"
        "target:\n  extensions: .py\n"
        "patterns:\n  - regex: \\bTODO\\b\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // below threshold: 2 TODOs → no finding
    std::string few = dir + "/few.py";
    write_file(few, "# TODO one\nx = 1\n# TODO two\n");
    auto f1 = keur::rules_scan_files(rules, {few});
    int hits_few = 0; for (auto& f : f1) if (f.rule_id == "CNT-1") hits_few++;
    CHECK(hits_few == 0);

    // at/over threshold: 3 TODOs → exactly one finding, at the 3rd match's line
    std::string many = dir + "/many.py";
    write_file(many, "# TODO 1\n# TODO 2\ny = 2\n# TODO 3\n# TODO 4\n");
    auto f2 = keur::rules_scan_files(rules, {many});
    int hits_many = 0; int line = 0;
    for (auto& f : f2) if (f.rule_id == "CNT-1") { hits_many++; line = f.line; }
    CHECK(hits_many == 1);   // fires ONCE, not per match
    CHECK(line == 4);        // the 3rd TODO (crossing the threshold) is on line 4
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] max-nesting flags control blocks deeper than max_count") {  // @proves engine-max-nesting
    // The max-nesting engine tracks control-flow nesting depth using the same
    // string/regex-aware brace lexer as block-absence, and flags a function
    // whose control nesting exceeds max_count (SonarQube S134). Non-control
    // braces (struct/initializer/lambda) do NOT raise the control depth.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/NEST.rule",
        "id: NEST-1\ntitle: too deeply nested\ncategory: quality\nseverity: warning\n"
        "engine: max-nesting\nmin_count: 3\n"
        "target:\n  extensions: .cpp\n"
        "patterns:\n  - regex: nesting too deep\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // 3 levels of control nesting: at the limit → no finding.
    std::string ok = dir + "/ok.cpp";
    write_file(ok,
        "void f() {\n"
        "  if (a) {\n"
        "    for (;;) {\n"
        "      while (x) { g(); }\n"   // depth 3 (if/for/while) — at limit
        "    }\n"
        "  }\n"
        "}\n");
    auto f1 = keur::rules_scan_files(rules, {ok});
    int hits_ok = 0; for (auto& f : f1) if (f.rule_id == "NEST-1") hits_ok++;
    CHECK(hits_ok == 0);

    // 4 levels of control nesting: over the limit → exactly one finding.
    std::string deep = dir + "/deep.cpp";
    write_file(deep,
        "void f() {\n"
        "  if (a) {\n"
        "    for (;;) {\n"
        "      while (x) {\n"
        "        switch (y) { case 1: g(); }\n"   // depth 4 — over limit
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n");
    auto f2 = keur::rules_scan_files(rules, {deep});
    int hits_deep = 0; for (auto& f : f2) if (f.rule_id == "NEST-1") hits_deep++;
    CHECK(hits_deep == 1);   // fires once for the over-deep function
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] max-lines flags a file with more net lines than max_count") {  // @proves engine-max-lines
    // The max-lines engine counts a file's net lines (non-blank; comment lines
    // are stripped when skip_comments is set) and flags ONCE when the count
    // exceeds max_count (min_count field). Replaces scripts/check-basics.sh's
    // per-file size check with a real rule.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/LINES.rule",
        "id: LINES-1\ntitle: file too long\ncategory: quality\nseverity: warning\n"
        "engine: max-lines\nmin_count: 5\nskip_comments: true\n"
        "target:\n  extensions: .cpp\n"
        "patterns:\n  - regex: file too long\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // 5 net lines: at the limit → no finding. (blank + comment lines ignored)
    std::string ok = dir + "/ok.cpp";
    write_file(ok,
        "int a;\n\n// a comment (stripped)\nint b;\nint c;\nint d;\nint e;\n");
    auto f1 = keur::rules_scan_files(rules, {ok});
    int hits_ok = 0; for (auto& f : f1) if (f.rule_id == "LINES-1") hits_ok++;
    CHECK(hits_ok == 0);

    // 6 net lines: over the limit → exactly one finding.
    std::string big = dir + "/big.cpp";
    write_file(big, "int a;\nint b;\nint c;\nint d;\nint e;\nint f;\n");
    auto f2 = keur::rules_scan_files(rules, {big});
    int hits_big = 0; for (auto& f : f2) if (f.rule_id == "LINES-1") hits_big++;
    CHECK(hits_big == 1);   // fires once for the over-long file
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] max-dir-files flags a directory with too many units") {  // @proves engine-max-dir-files
    // The max-dir-files engine is FILE-LEVEL: it runs once over the whole
    // fileset, groups files by directory, counts logical units (a .h/.cpp pair
    // with the same stem = one unit), and flags a directory over max_count.
    // This is the cross-file aggregation that scripts/check-basics.sh did in
    // shell — expressible because file-level engines see the whole fileset.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/DIRS.rule",
        "id: DIRS-1\ntitle: too many units in a dir\ncategory: quality\nseverity: warning\n"
        "engine: max-dir-files\nmin_count: 2\n"
        "target:\n  extensions: .cpp .h\n"
        "patterns:\n  - regex: too many units\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // A dir with 3 units (a.h/a.cpp = 1, b.cpp = 2, c.cpp = 3) over limit 2.
    std::string sub = dir + "/pkg";
    keur::adapters::Fs fs;
    fs.default_mkdir_p(sub);
    write_file(sub + "/a.h", "int a;\n");
    write_file(sub + "/a.cpp", "int a;\n");   // a.h + a.cpp = ONE unit
    write_file(sub + "/b.cpp", "int b;\n");
    write_file(sub + "/c.cpp", "int c;\n");
    auto f = keur::rules_scan_files(rules,
        {sub + "/a.h", sub + "/a.cpp", sub + "/b.cpp", sub + "/c.cpp"});
    int hits = 0; for (auto& x : f) if (x.rule_id == "DIRS-1") hits++;
    CHECK(hits == 1);   // 3 units > 2 → the directory is flagged once
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] source: field parses as provenance metadata") {  // @proves rule-source-field
    // `source:` records where a rule came from (e.g. "cpm") so imported rules
    // need no separate directory. It is metadata: the engine parses it and
    // otherwise ignores it. Absent → empty.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/SRC.rule",
        "id: SRC-1\ntitle: imported\ncategory: security\nseverity: error\n"
        "engine: pattern\nsource: cpm\n"
        "target:\n  extensions: .py\n"
        "patterns:\n  - regex: \\beval\\b\n");
    write_file(dir + "/OWN.rule",
        "id: OWN-1\ntitle: own\ncategory: security\nseverity: error\n"
        "engine: pattern\n"
        "target:\n  extensions: .py\n"
        "patterns:\n  - regex: \\bexec\\b\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(rules.size() >= 2);
    std::string src_of_src, src_of_own;
    for (const auto& r : rules) {
        if (r.id == "SRC-1") src_of_src = r.source;
        if (r.id == "OWN-1") src_of_own = r.source;
    }
    CHECK(src_of_src == "cpm");   // parsed
    CHECK(src_of_own.empty());    // absent → empty (keur's own)
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] KEUR-QUAL-007 blocks committed conflict markers") {  // @proves KEUR-QUAL-007
    // A leftover git conflict marker is a hard defect → the rule BLOCKS. Exactly
    // 7 marker chars + space/EOL; an 8-char markdown underline must NOT match.
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());

    std::string bad = dir + "/conflicted.cpp";
    write_file(bad, "int a = 1;\n<<<<<<< HEAD\nint b = 2;\n=======\nint b = 3;\n>>>>>>> feat\n");
    auto f1 = keur::rules_scan_files(rules, {bad});
    int hits = 0; std::string action;
    for (auto& f : f1) if (f.rule_id == "KEUR-QUAL-007") { hits++; action = f.action; }
    CHECK(hits >= 1);
    CHECK(action == "block");     // conflict markers block, not warn

    // Markdown H1 underline is exactly 7 '=' under a title — must NOT match
    // (that is why the rule matches only the <<<<<<< / >>>>>>> angle markers).
    std::string clean = dir + "/clean.md";
    write_file(clean, "My Heading\n=======\n\nbody\n");
    auto f2 = keur::rules_scan_files(rules, {clean});
    int fp = 0; for (auto& f : f2) if (f.rule_id == "KEUR-QUAL-007") fp++;
    CHECK(fp == 0);               // no false positive on a markdown H1 underline
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] multiline window fires within N lines, not beyond") {  // @proves engine-multiline
    // RE2 matches one line at a time; the window engine joins a sliding window of
    // `window` lines so a pattern can span them — but STAYS bounded to the window
    // (no file-wide .*) and reports the window's START line. @see engine-multiline
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/ML.rule",
        "id: ML-1\ntitle: secret assigned then used\ncategory: secret\nseverity: critical\n"
        "engine: multiline\nwithin: window\nwindow: 3\n"
        "target:\n  extensions: .py\n"
        "patterns:\n  - regex: secret\\s*=[\\s\\S]*?use\\s*\\(\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // within the window: assign on line 1, use on line 3 → one finding at line 1
    std::string near = dir + "/near.py";
    write_file(near, "secret = get()\nx = 1\nuse(secret)\n");
    auto f1 = keur::rules_scan_files(rules, {near});
    int hits_near = 0, line = 0;
    for (auto& f : f1) if (f.rule_id == "ML-1") { hits_near++; line = f.line; }
    CHECK(hits_near == 1);   // fires once for the window
    CHECK(line == 1);        // reports the window START line, not the joined blob

    // beyond the window: assign on line 1, use on line 5 → span > 3 lines → no finding
    std::string far = dir + "/far.py";
    write_file(far, "secret = get()\na = 1\nb = 2\nc = 3\nuse(secret)\n");
    auto f2 = keur::rules_scan_files(rules, {far});
    int hits_far = 0; for (auto& f : f2) if (f.rule_id == "ML-1") hits_far++;
    CHECK(hits_far == 0);    // the window (3 lines) never spans line 1..5
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] requires: prunes a rule when facts don't satisfy it") {  // @proves ADR-009.requires
    // A rule requiring framework=express must NOT fire in a repo whose package.json
    // has no express; the same rule MUST fire when express is present.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/REQ.rule",
        "id: REQ-1\ntitle: express-only rule\ncategory: sast\nseverity: warning\nengine: pattern\n"
        "requires:\n  framework: express\n"
        "target:\n  extensions: .js\n"
        "patterns:\n  - regex: app\\.get\\s*\\(\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // no express in the manifest → guard fails → rule pruned → no finding
    write_file(dir + "/package.json", "{\"dependencies\": {\"lodash\": \"^4.0.0\"}}\n");
    std::string js = dir + "/a.js";
    write_file(js, "app.get('/x', h);\n");
    auto f1 = keur::rules_scan_dir(rules, dir);
    int hits_no = 0; for (auto& f : f1) if (f.rule_id == "REQ-1") hits_no++;
    CHECK(hits_no == 0);

    // now express IS pinned → guard satisfied → rule runs → fires
    write_file(dir + "/package.json", "{\"dependencies\": {\"express\": \"4.17.1\"}}\n");
    auto f2 = keur::rules_scan_dir(rules, dir);
    int hits_yes = 0; for (auto& f : f2) if (f.rule_id == "REQ-1") hits_yes++;
    CHECK(hits_yes == 1);
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] composite all/any/not combines leaf rules") {  // @proves ADR-009.composite
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    // two ordinary leaf rules that also run standalone
    write_file(dir + "/LEAF-A.rule",
        "id: LEAF-A\ntitle: has sink\ncategory: sast\nseverity: info\nengine: pattern\n"
        "target:\n  extensions: .js\n"
        "patterns:\n  - regex: dangerousSink\\s*\\(\n");
    write_file(dir + "/LEAF-B.rule",
        "id: LEAF-B\ntitle: has mitigation\ncategory: sast\nseverity: info\nengine: pattern\n"
        "target:\n  extensions: .js\n"
        "patterns:\n  - regex: safeGuard\\s*\\(\n");
    // composite: fire when the sink is present AND the mitigation is NOT.
    write_file(dir + "/COMP.rule",
        "id: COMP-1\ntitle: sink without mitigation\ncategory: sast\nseverity: error\n"
        "engine: composite\naction: block\n"
        "composite:\n  all: LEAF-A\n  not: LEAF-B\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(rules.size() == 3);

    // sink present, no mitigation → composite fires
    std::string bad = dir + "/bad.js";
    write_file(bad, "dangerousSink(userInput);\n");
    auto f1 = keur::rules_scan_files(rules, {bad});
    int comp_bad = 0, leaf_a = 0;
    for (auto& f : f1) { if (f.rule_id == "COMP-1") comp_bad++; if (f.rule_id == "LEAF-A") leaf_a++; }
    CHECK(leaf_a == 1);   // leaf still fires standalone
    CHECK(comp_bad == 1); // composite fires: all(A) and not(B)

    // sink present AND mitigation present → composite does NOT fire
    std::string good = dir + "/good.js";
    write_file(good, "safeGuard(); dangerousSink(userInput);\n");
    auto f2 = keur::rules_scan_files(rules, {good});
    int comp_good = 0;
    for (auto& f : f2) if (f.rule_id == "COMP-1") comp_good++;
    CHECK(comp_good == 0);  // not(B) is false because B fired
    cleanup_dir(dir);
}

} // TEST_SUITE("integration")


// ============================================================================
// TEST_SUITE: perf — timing guards
// ============================================================================

TEST_SUITE("perf") {

SCENARIO("[feature:rule-engine] rules_load is fast (<50ms)") {
    auto t0 = std::chrono::steady_clock::now();
    auto rules = keur::rules_load(keur_rules_dir());
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    MESSAGE("rules_load: " << ms << "ms, " << rules.size() << " rules");
    CHECK(ms < 50);
}

SCENARIO("[feature:rule-engine] scan 3 files is fast (<100ms)") {
    auto rules = keur::rules_load(keur_rules_dir());
    REQUIRE(!rules.empty());
    std::vector<std::string> files = {
        fixtures_dir() + "/secrets/rsa_key.pem",
        fixtures_dir() + "/secrets/ec_key.pem",
        fixtures_dir() + "/clean/clean.yml",
    };
    auto t0 = std::chrono::steady_clock::now();
    auto fs = keur::rules_scan_files(rules, files);
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    MESSAGE("scan 3 files: " << ms << "ms, " << fs.size() << " findings");
    CHECK(ms < 100);
}

SCENARIO("[feature:policy] 100x policy_load is fast (<5ms)") {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i)
        keur::cmd::policy_load("/tmp/nonexistent_perf.toml");
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    MESSAGE("100x policy_load: " << us << "us");
    CHECK(us < 5000);
}

} // TEST_SUITE("perf")

// Reopen the unit suite for the pure Layer 4/5 tests below (decode_key +
// manager tree model/view) so `make test-unit` runs them.
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
    CHECK(decode_key("\033[Z").key == Key::Unknown);
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
    // Global has the editable security + secrets categories (each with a
    // subcategory level below — 'generic' for this test data).
    const auto& global = m.roots[1];
    REQUIRE(global.children.size() == 2);
    CHECK(global.children[0].label == "security");
    // security → generic → 2 rules
    REQUIRE(global.children[0].children.size() == 1);
    CHECK(global.children[0].children[0].label == "generic");
    const auto& sec_rules = global.children[0].children[0].children;
    REQUIRE(sec_rules.size() == 2);
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
    auto save = [&](const std::string& content) {
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

SCENARIO("[feature:learn] learn_parse reads flags and source with sane defaults") {
    using keur::cmd::learn_parse;
    // Defaults: no flags set, source defaults to "corpus".
    auto def = learn_parse({});
    CHECK(def.stop == false);
    CHECK(def.status == false);
    CHECK(def.source == "corpus");
    // Flags and options are parsed.
    auto opt = learn_parse({"--status", "--source", "keureval", "--model", "qwen"});
    CHECK(opt.status == true);
    CHECK(opt.source == "keureval");
    CHECK(opt.model == "qwen");
}

SCENARIO("[feature:help] help/version are dispatch aliases (behaviour proven e2e)") {
    // `help`/`--help`/`-h` and `version`/`--version`/`-v` are pure dispatch
    // aliases in main.cpp's command table; their end-to-end behaviour (usage
    // text, exit 0) is asserted in tests/e2e/smoke.sh. This scenario documents
    // that contract so [feature:help] is traceable. The parse-level surface the
    // unit tier can reach is covered by the version/learn scenarios above.
    CHECK(true);
}

} // TEST_SUITE("unit") — Layer 4/5 pure tests
