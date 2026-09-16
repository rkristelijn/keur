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
#include "cmd/cmd_check.h"
#include "cmd/cmd_hook.h"
#include "cmd/cmd_policy.h"
#include "cmd/cmd_scan.h"
#include "adapters/fs.h"
#include "adapters/git.h"
#include "util/keur_utils.h"
#include "adapters/proc.h"
#include "report/report.h"

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

static keur::Fs make_fake_fs(FakeFsState& st) {
    keur::Fs fs;
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
    auto p = keur::policy_load("/tmp/keur_no_such_file_xyz.toml");
    CHECK(p.block_at == doctest::Approx(0.85));
    CHECK(p.warn_at  == doctest::Approx(0.50));
    CHECK(p.version.empty());
}

SCENARIO("[feature:policy] policy_load reads custom thresholds") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    write_file(tmp + "/keur.toml",
               "version = \"1.2.3\"\nblock-at = 0.95\nwarn-at = 0.60\n");
    auto p = keur::policy_load(tmp + "/keur.toml");
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
    auto p = keur::policy_load(tmp + "/keur.toml");
    CHECK(p.version  == "2.0.0");
    CHECK(p.block_at == doctest::Approx(0.90));
    cleanup_dir(tmp);
}

SCENARIO("[feature:policy] resolve_config returns hint unchanged") {
    CHECK(keur::resolve_config("/tmp/explicit.toml") == "/tmp/explicit.toml");
}

SCENARIO("[feature:policy] resolve_config falls back to XDG path") {
    auto r = keur::resolve_config("");
    CHECK(r.find("keur/keur.toml") != std::string::npos);
}

SCENARIO("[feature:policy] cmd_policy_parse defaults to explain") {
    CHECK(keur::cmd_policy_parse({}).subcommand == "explain");
}

SCENARIO("[feature:policy] cmd_policy_parse accepts --config") {
    auto o = keur::cmd_policy_parse({"--config", "/p", "explain"});
    CHECK(o.config == "/p");
    CHECK(o.subcommand == "explain");
}

SCENARIO("[feature:policy] cmd_policy returns 1 for unknown subcommand") {
    keur::PolicyOptions o; o.subcommand = "bad";
    CHECK(keur::cmd_policy(o) == 1);
}

SCENARIO("[feature:policy] cmd_policy explain returns 0") {
    keur::PolicyOptions o;
    CHECK(keur::cmd_policy(o) == 0);
}

// --- [feature:check] --------------------------------------------------------

SCENARIO("[feature:check] policy_evaluate — no findings → Pass") {
    keur::PolicyInput in{};
    CHECK(keur::policy_evaluate(in) == keur::Verdict::Pass);
}

SCENARIO("[feature:check] policy_evaluate — finding only → Warn") {
    keur::PolicyInput in{}; in.has_any_finding = true;
    CHECK(keur::policy_evaluate(in) == keur::Verdict::Warn);
}

SCENARIO("[feature:check] policy_evaluate — rule block → Block") {
    keur::PolicyInput in{}; in.has_any_finding = true; in.has_rule_block = true;
    CHECK(keur::policy_evaluate(in) == keur::Verdict::Block);
}

SCENARIO("[feature:check] policy_evaluate — secret block → Block") {
    keur::PolicyInput in{}; in.has_secret_block = true;
    CHECK(keur::policy_evaluate(in) == keur::Verdict::Block);
}

SCENARIO("[feature:check] cmd_check_parse defaults") {
    auto o = keur::cmd_check_parse({});
    CHECK(o.tier == keur::Tier::PreCommit);
    CHECK(o.ai   == "off");
    CHECK(o.config.empty());
}

SCENARIO("[feature:check] cmd_check_parse --tier pre-push") {
    CHECK(keur::cmd_check_parse({"--tier", "pre-push"}).tier == keur::Tier::PrePush);
}

SCENARIO("[feature:check] cmd_check_parse --tier pre-commit explicit") {
    CHECK(keur::cmd_check_parse({"--tier", "pre-commit"}).tier == keur::Tier::PreCommit);
}

SCENARIO("[feature:check] cmd_check_parse --ai") {
    CHECK(keur::cmd_check_parse({"--ai", "ollama"}).ai == "ollama");
}

SCENARIO("[feature:check] cmd_check_parse --config") {
    CHECK(keur::cmd_check_parse({"--config", "/c"}).config == "/c");
}

SCENARIO("[feature:check] cmd_check_parse ignores unknown flags") {
    CHECK(keur::cmd_check_parse({"--nope", "x"}).tier == keur::Tier::PreCommit);
}

SCENARIO("[feature:check] cmd_check PASS with empty file source") {
    keur::CheckOptions opts;
    CHECK(keur::cmd_check(opts, []() { return std::vector<std::string>{}; }) == 0);
}

SCENARIO("[feature:check] staged_files returns empty outside a git repo") {
    char orig[4096];
    REQUIRE(getcwd(orig, sizeof(orig)) != nullptr);
    chdir("/tmp");
    CHECK(keur::staged_files().empty());
    chdir(orig);
}

// --- [feature:scan] ---------------------------------------------------------

SCENARIO("[feature:scan] cmd_scan_parse defaults to current dir") {
    auto o = keur::cmd_scan_parse({});
    CHECK(o.target == ".");
    CHECK(!o.json);
}

SCENARIO("[feature:scan] cmd_scan_parse positional path") {
    CHECK(keur::cmd_scan_parse({"/repo"}).target == "/repo");
}

SCENARIO("[feature:scan] cmd_scan_parse --json") {
    CHECK(keur::cmd_scan_parse({"--json"}).json);
}

SCENARIO("[feature:scan] cmd_scan_parse --config") {
    CHECK(keur::cmd_scan_parse({"--config", "/p"}).config == "/p");
}

SCENARIO("[feature:scan] cmd_scan returns 0 with no rules") {
    unsetenv("KEUR_HOME");
    unsetenv("KEUR_RULES_DIRS");
    keur::ScanOptions o; o.target = "/tmp";
    CHECK(keur::cmd_scan(o) == 0);
}

// --- [feature:hook] ---------------------------------------------------------

SCENARIO("[feature:hook] cmd_hook_parse defaults to Install") {
    auto o = keur::cmd_hook_parse({});
    CHECK(o.action == keur::HookAction::Install);
    CHECK(!o.global);
}

SCENARIO("[feature:hook] cmd_hook_parse --install") {
    CHECK(keur::cmd_hook_parse({"--install"}).action == keur::HookAction::Install);
}

SCENARIO("[feature:hook] cmd_hook_parse --global") {
    CHECK(keur::cmd_hook_parse({"--global"}).action == keur::HookAction::Global);
}

SCENARIO("[feature:hook] cmd_hook_parse --status") {
    CHECK(keur::cmd_hook_parse({"--status"}).action == keur::HookAction::Status);
}

SCENARIO("[feature:hook] cmd_hook_parse --remove") {
    auto o = keur::cmd_hook_parse({"--remove"});
    CHECK(o.action == keur::HookAction::Remove);
    CHECK(!o.global);
}

SCENARIO("[feature:hook] cmd_hook_parse --remove --global") {
    auto o = keur::cmd_hook_parse({"--remove", "--global"});
    CHECK(o.action == keur::HookAction::Remove);
    CHECK(o.global);
}

SCENARIO("[feature:hook] hooks_install creates executable stubs") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    std::string hdir = tmp + "/hooks";
    CHECK(keur::hooks_install(hdir));

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
    keur::hooks_install(hdir);
    REQUIRE(file_exists_at(hdir + "/pre-commit"));
    keur::hooks_remove(hdir);
    CHECK(!file_exists_at(hdir + "/pre-commit"));
    CHECK(!file_exists_at(hdir + "/pre-push"));
    cleanup_dir(tmp);
}

SCENARIO("[feature:hook] hooks_remove idempotent on empty dir") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    keur::hooks_remove(tmp);
    CHECK(true);
    cleanup_dir(tmp);
}

SCENARIO("[feature:hook] hooks_install idempotent") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    std::string hdir = tmp + "/hooks";
    CHECK(keur::hooks_install(hdir));
    CHECK(keur::hooks_install(hdir));
    cleanup_dir(tmp);
}

SCENARIO("[feature:hook] global_hooks_dir respects XDG_CONFIG_HOME") {
    setenv("XDG_CONFIG_HOME", "/custom/xdg", 1);
    CHECK(keur::global_hooks_dir().find("/custom/xdg") == 0);
    unsetenv("XDG_CONFIG_HOME");
}

SCENARIO("[feature:hook] global_hooks_dir falls back to ~/.config") {
    unsetenv("XDG_CONFIG_HOME");
    CHECK(keur::global_hooks_dir().find("git/hooks") != std::string::npos);
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
    CHECK(keur::rule_dirs().empty());
}

SCENARIO("[feature:rule-engine] rule_dirs reads KEUR_HOME") {
    setenv("KEUR_HOME", "/opt/keur", 1);
    unsetenv("KEUR_RULES_DIRS");
    auto d = keur::rule_dirs();
    REQUIRE(d.size() == 1);
    CHECK(d[0] == "/opt/keur/rules");
    unsetenv("KEUR_HOME");
}

SCENARIO("[feature:rule-engine] rule_dirs splits KEUR_RULES_DIRS on colon") {
    unsetenv("KEUR_HOME");
    setenv("KEUR_RULES_DIRS", "/a:/b:/c", 1);
    auto d = keur::rule_dirs();
    REQUIRE(d.size() == 3);
    CHECK(d[0] == "/a");
    CHECK(d[1] == "/b");
    CHECK(d[2] == "/c");
    unsetenv("KEUR_RULES_DIRS");
}

// --- [feature:git] git adapter (injected fake, no real git) -----------------

SCENARIO("[feature:git] git_repo_hooks_dir appends /hooks to git-dir") {
    keur::Git fake;
    fake.run = [](const std::string& args) -> std::string {
        if (args == "rev-parse --git-dir") return "/repo/.git";
        return "";
    };
    CHECK(keur::git_repo_hooks_dir(fake) == "/repo/.git/hooks");
}

SCENARIO("[feature:git] git_repo_hooks_dir empty outside a repo") {
    keur::Git fake;
    fake.run = [](const std::string&) -> std::string { return ""; };
    CHECK(keur::git_repo_hooks_dir(fake).empty());
}

SCENARIO("[feature:git] default_run trims output and runs real git") {
    // Safe, repo-independent git query.
    keur::Git git;  // real implementation
    auto out = git.run("--version");
    CHECK(out.find("git") != std::string::npos);
    // Trimmed: no trailing newline.
    CHECK((out.empty() || out.back() != '\n'));
}

SCENARIO("[feature:git] default_run returns empty for a bogus subcommand") {
    keur::Git git;
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
    auto json = keur::finding_to_json(f);
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
    CHECK(keur::finding_to_json(f).find("\"confidence\":0.5") != std::string::npos);
}

SCENARIO("[feature:report] report_findings JSONL emits one line per finding") {
    std::vector<keur::RuleFinding> fs(2);
    fs[0].rule_id = "A"; fs[0].line = 1;
    fs[1].rule_id = "B"; fs[1].line = 2;
    std::ostringstream out;
    keur::ReportOptions ro; ro.format = keur::OutputFormat::Jsonl;
    auto n = keur::report_findings(out, fs, ro);
    CHECK(n == 2);
    std::string s = out.str();
    CHECK(std::count(s.begin(), s.end(), '\n') == 2);
    CHECK(s.find("\"rule\":\"A\"") != std::string::npos);
    CHECK(s.find("\"rule\":\"B\"") != std::string::npos);
}

SCENARIO("[feature:report] report_findings compact emits file:line RULE") {
    std::vector<keur::RuleFinding> fs(1);
    fs[0].rule_id = "R"; fs[0].file = "a.txt"; fs[0].line = 7; fs[0].message = "msg";
    std::ostringstream out;
    keur::ReportOptions ro; ro.format = keur::OutputFormat::Compact;
    ro.color = keur::ColorMode::Never;
    keur::report_findings(out, fs, ro);
    std::string s = out.str();
    CHECK(s.find("a.txt:7 R") != std::string::npos);
    CHECK(s.find("msg") != std::string::npos);
}

SCENARIO("[feature:report] report_findings verbose includes action and title") {
    std::vector<keur::RuleFinding> fs(1);
    fs[0].rule_id = "R"; fs[0].action = "warn"; fs[0].title = "T";
    fs[0].file = "a"; fs[0].line = 1; fs[0].message = "m"; fs[0].confidence = "0.6";
    std::ostringstream out;
    keur::ReportOptions ro; ro.format = keur::OutputFormat::Verbose;
    ro.color = keur::ColorMode::Never;
    keur::report_findings(out, fs, ro);
    std::string s = out.str();
    CHECK(s.find("[warn] R") != std::string::npos);
    CHECK(s.find("- T") != std::string::npos);
    CHECK(s.find("confidence: 0.6") != std::string::npos);
}

SCENARIO("[feature:report] use_color Never is always false") {
    CHECK(!keur::use_color(keur::ColorMode::Never, true));
}

SCENARIO("[feature:report] use_color Always is true") {
    CHECK(keur::use_color(keur::ColorMode::Always, false));
}

SCENARIO("[feature:report] use_color Auto is false without a tty") {
    unsetenv("NO_COLOR");
    CHECK(!keur::use_color(keur::ColorMode::Auto, false));
}

SCENARIO("[feature:report] use_color Auto respects NO_COLOR") {
    setenv("NO_COLOR", "1", 1);
    CHECK(!keur::use_color(keur::ColorMode::Auto, true));
    unsetenv("NO_COLOR");
}

// --- [feature:fs] filesystem adapter (real impl over a tempdir) -------------

SCENARIO("[feature:fs] default_mkdir_p / write_executable / exists / remove") {
    keur::Fs fs;  // real implementation
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
    keur::Fs fs;
    CHECK(!fs.exists("/tmp/keur_definitely_missing_xyz_123"));
}

// --- [feature:proc] process adapter (real impl, safe commands) --------------

SCENARIO("[feature:proc] default_run returns 0 for true, non-zero for false") {
    keur::Proc proc;
    CHECK(proc.run("true") == 0);
    CHECK(proc.run("false") != 0);
}

SCENARIO("[feature:proc] default_capture captures stdout and exit code") {
    keur::Proc proc;
    auto r = proc.capture("echo hello");
    CHECK(r.exit_code == 0);
    CHECK(r.stdout_text.find("hello") != std::string::npos);
}

SCENARIO("[feature:proc] default_has finds sh but not a bogus binary") {
    keur::Proc proc;
    CHECK(proc.has("sh"));
    CHECK(!proc.has("keur_bogus_binary_xyz_123"));
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

SCENARIO("[feature:rule-engine] custom pattern rule fires on trigger file") {
    auto rules = keur::rules_load(fixtures_dir() + "/rules");
    REQUIRE(!rules.empty());
    std::string dir;
    auto path = stage_fixture("rules/trigger.txt", "src.c", dir);
    auto fs = keur::rules_scan_files(rules, {path});
    CHECK(has_rule(fs, "TEST-PAT-001"));
    cleanup_dir(dir);
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
    keur::CheckOptions opts;
    auto rc = keur::cmd_check(opts, [&]() {
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
    keur::CheckOptions opts;
    auto rc = keur::cmd_check(opts, [&]() {
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

    keur::ScanOptions o;
    o.target = base;
    auto rc = keur::cmd_scan(o);   // exercises the JSONL output loop
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

    keur::ScanOptions o;
    o.target = base;
    CHECK(keur::cmd_scan(o) == 0);

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
    keur::HookDeps deps;
    deps.git.run = [](const std::string& args) -> std::string {
        return args == "rev-parse --git-dir" ? "/repo/.git" : "";
    };
    deps.fs = make_fake_fs(st);

    keur::HookOptions install; install.action = keur::HookAction::Install;
    CHECK(keur::cmd_hook(install, deps) == 0);
    REQUIRE(st.written.size() == 2);
    CHECK(st.written[0] == "/repo/.git/hooks/pre-commit");
    CHECK(st.written[1] == "/repo/.git/hooks/pre-push");
}

SCENARIO("[feature:hook] cmd_hook status queries fs.exists via the fake") {
    FakeFsState st;
    keur::HookDeps deps;
    deps.git.run = [](const std::string& args) -> std::string {
        return args == "rev-parse --git-dir" ? "/repo/.git" : "";
    };
    deps.fs = make_fake_fs(st);
    keur::HookOptions status; status.action = keur::HookAction::Status;
    CHECK(keur::cmd_hook(status, deps) == 0);
}

SCENARIO("[feature:hook] cmd_hook remove deletes via the fake fs") {
    FakeFsState st;
    // Pretend the stubs already exist so remove() is called.
    st.written = {"/repo/.git/hooks/pre-commit", "/repo/.git/hooks/pre-push"};
    keur::HookDeps deps;
    deps.git.run = [](const std::string& args) -> std::string {
        return args == "rev-parse --git-dir" ? "/repo/.git" : "";
    };
    deps.fs = make_fake_fs(st);
    keur::HookOptions remove; remove.action = keur::HookAction::Remove;
    CHECK(keur::cmd_hook(remove, deps) == 0);
    REQUIRE(st.removed.size() == 2);
}

SCENARIO("[feature:hook] cmd_hook --global installs and sets hooksPath via fake proc") {
    FakeFsState st;
    std::vector<std::string> proc_calls;
    keur::HookDeps deps;
    deps.git.run = [](const std::string&) -> std::string {
        return "";  // no existing core.hooksPath
    };
    deps.fs = make_fake_fs(st);
    deps.proc.run = [&proc_calls](const std::string& cmd) {
        proc_calls.push_back(cmd);  // capture instead of executing
        return 0;
    };

    keur::HookOptions g; g.action = keur::HookAction::Global;
    CHECK(keur::cmd_hook(g, deps) == 0);
    // Stubs written to the global dir + git config invoked via proc (not run).
    REQUIRE(st.written.size() == 2);
    REQUIRE(proc_calls.size() == 1);
    CHECK(proc_calls[0].find("core.hooksPath") != std::string::npos);
}

SCENARIO("[feature:hook] cmd_hook --global leaves an existing hooksPath alone") {
    FakeFsState st;
    std::vector<std::string> proc_calls;
    keur::HookDeps deps;
    deps.git.run = [](const std::string&) -> std::string {
        return "/some/other/hooks";  // already set to a different dir
    };
    deps.fs = make_fake_fs(st);
    deps.proc.run = [&proc_calls](const std::string& cmd) {
        proc_calls.push_back(cmd);
        return 0;
    };
    keur::HookOptions g; g.action = keur::HookAction::Global;
    CHECK(keur::cmd_hook(g, deps) == 0);
    // Must NOT reconfigure git when a different hooksPath is already set.
    CHECK(proc_calls.empty());
}

SCENARIO("[feature:hook] cmd_hook install returns 1 when fake git reports no repo") {
    keur::HookDeps deps;
    deps.git.run = [](const std::string&) -> std::string { return ""; };
    keur::HookOptions install; install.action = keur::HookAction::Install;
    CHECK(keur::cmd_hook(install, deps) == 1);
}

SCENARIO("[feature:hook] cmd_hook remove returns 1 when fake git reports no repo") {
    keur::HookDeps deps;
    deps.git.run = [](const std::string&) -> std::string { return ""; };
    keur::HookOptions remove; remove.action = keur::HookAction::Remove;
    CHECK(keur::cmd_hook(remove, deps) == 1);
}

SCENARIO("[feature:hook] hooks_install returns false when fake fs mkdir fails") {
    keur::Fs fs;
    fs.mkdir_p = [](const std::string&) { return false; };  // simulate failure
    CHECK(!keur::hooks_install("/whatever", fs));
}

SCENARIO("[feature:check] staged_files uses the injected fake git") {
    keur::Git fake;
    fake.staged_files = []() {
        return std::vector<std::string>{"a.txt", "b.txt"};
    };
    auto files = keur::staged_files(fake);
    REQUIRE(files.size() == 2);
    CHECK(files[0] == "a.txt");
    CHECK(files[1] == "b.txt");
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
        keur::policy_load("/tmp/nonexistent_perf.toml");
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    MESSAGE("100x policy_load: " << us << "us");
    CHECK(us < 5000);
}

} // TEST_SUITE("perf")
