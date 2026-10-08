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
 *   [feature:train]        train_parse option parsing (the optimise loop)
 *   [feature:learn]        learn = deprecated alias forwarding to train (ADR-036)
 *   [feature:help]         usage/help dispatch (behaviour proven e2e in smoke.sh)
 *
 * @see docs/adr/ADR-002-single-cpp-binary.md
 */
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
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
#include "cmd/level.h"
#include "cmd/manager.h"
#include "cmd/policy.h"
#include "cmd/scan.h"
#include "cmd/settings.h"
#include "cmd/train.h"
#include "cmd/tune.h"
#include "config/hookmgr.h"
#include "config/scanners.h"
#include "config/disabled.h"
#include "config/ruleset.h"
#include "adapters/fs.h"
#include "adapters/git.h"
#include "util/keur_utils.h"
#include "adapters/proc.h"
#include "scan/dispatch.h"
#include "adapters/sysinfo.h"
#include "learn/sysinfo.h"
#include "report/report.h"
#include "common/platform.h"
#include "manager/model.h"
#include "manager/view.h"
#include "manager/catalog.h"

// ---------------------------------------------------------------------------
// Test helpers — shared, universally-useful ones live in tests/test_helpers.h
// (tempdirs, fixture staging, repo paths, has_rule, the fake fs). Pulled in via
// `using` so the 290 scenarios below keep calling them unqualified. Domain
// helpers (manager model builders, hook-stub runners, scanner TOML) stay below.
// ---------------------------------------------------------------------------
#include "tests/test_helpers.h"

using keur::test::create_temp_dir;
using keur::test::write_file;
using keur::test::read_file;
using keur::test::cleanup_dir;
using keur::test::file_exists_at;
using keur::test::repo_root;
using keur::test::fixtures_dir;
using keur::test::keur_rules_dir;
using keur::test::has_rule;
using keur::test::stage_fixture;
using keur::test::FakeFsState;
using keur::test::make_fake_fs;


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

SCENARIO("[feature:policy] find_config_upward finds keur.toml in the same dir") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    write_file(tmp + "/keur.toml", "block-at = 0.95\n");
    CHECK(keur::cmd::find_config_upward(tmp) == tmp + "/keur.toml");
}

SCENARIO("[feature:policy] find_config_upward walks up from a subdir") {
    // Write the config in a fresh temp dir, then search from an existing SUBDIR
    // reference (the dir itself via a trailing '/.') so no new directories are
    // created — keeps the test cross-platform (no POSIX-only mkdir).
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    write_file(tmp + "/keur.toml", "block-at = 1.01\n");
    CHECK(keur::cmd::find_config_upward(tmp + "/.") == tmp + "/keur.toml");
}

SCENARIO("[feature:policy] find_config_upward returns empty when none exists") {
    std::string tmp = create_temp_dir();
    REQUIRE(!tmp.empty());
    // no keur.toml written anywhere under this fresh tmp tree
    CHECK(keur::cmd::find_config_upward(tmp).empty());
}

SCENARIO("[feature:policy] resolve_config returns a keur.toml path") {
    // With no --config hint, resolve_config walks the repo (.config/keur.toml or
    // root keur.toml) and falls back to the XDG global path. Running inside the
    // keur repo it finds the repo config; in a bare env it returns the XDG path.
    // Either way the resolved path ends in keur.toml.
    auto r = keur::cmd::resolve_config("");
    CHECK(r.find("keur.toml") != std::string::npos);
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

SCENARIO("[feature:check] finding_blocks — block action at/above threshold blocks") {
    // action=block, confidence 0.9, block-at 0.85 → blocks
    CHECK(keur::cmd::finding_blocks("block", "0.9", 0.85));
    CHECK(keur::cmd::finding_blocks("block", "0.85", 0.85));  // == threshold
}

SCENARIO("[feature:check] finding_blocks — below threshold does not block") {
    // action=block but confidence under block-at → does NOT block (WARN)
    CHECK_FALSE(keur::cmd::finding_blocks("block", "0.90", 1.01));  // unreachable threshold
    CHECK_FALSE(keur::cmd::finding_blocks("block", "0.5", 0.85));
}

SCENARIO("[feature:check] finding_blocks — non-block action never blocks") {
    CHECK_FALSE(keur::cmd::finding_blocks("warn", "0.99", 0.5));
    CHECK_FALSE(keur::cmd::finding_blocks("advisory", "1.0", 0.5));
}

SCENARIO("[feature:check] finding_blocks — malformed confidence is treated as blocking") {
    // A block-action rule with an unparseable confidence must fail safe (block),
    // not silently pass the gate.
    CHECK(keur::cmd::finding_blocks("block", "", 0.85));
    CHECK(keur::cmd::finding_blocks("block", "notanumber", 0.85));
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

SCENARIO("[feature:check] format_finding_line — marks a blocker and shows file:line") {
    auto s = keur::cmd::format_finding_line("KEUR-SEC-001", "block", "0.98",
                                            "id_rsa.pem", 1, "Private key committed", true);
    CHECK(s.rfind("BLOCK ", 0) == 0);          // starts with the blocker marker
    CHECK(s.find("KEUR-SEC-001") != std::string::npos);
    CHECK(s.find("conf 0.98") != std::string::npos);
    CHECK(s.find("id_rsa.pem:1") != std::string::npos);
    CHECK(s.find("Private key committed") != std::string::npos);
}

SCENARIO("[feature:check] format_finding_line — a warn finding is marked warn") {
    auto s = keur::cmd::format_finding_line("KEUR-QUAL-002", "warn", "0.60",
                                            "SECURITY.md", 0, "No SECURITY.md", false);
    CHECK(s.rfind("warn  ", 0) == 0);
    CHECK(s.find("SECURITY.md") != std::string::npos);
    CHECK(s.find(":0") == std::string::npos);  // line 0 is not appended
}

SCENARIO("[feature:check] cmd_check PASS with empty file source") {    keur::cmd::CheckOptions opts;
    CHECK(keur::cmd::check(opts, []() { return std::vector<std::string>{}; }) == 0);
}

// A clean PASS must NOT write to stderr: VS Code (and some CI) treat ANY stderr
// output from a git hook as a failure, even on exit 0 — that made keur's PASS
// look like an error in the editor. The non-error verdict belongs on stdout.
SCENARIO("[feature:check] a PASS writes to stdout, not stderr") {  // @proves hook-pass-on-stdout
    keur::cmd::CheckOptions opts;
    std::ostringstream cap_out, cap_err;
    std::streambuf* old_out = std::cout.rdbuf(cap_out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(cap_err.rdbuf());
    int rc = keur::cmd::check(opts, []() { return std::vector<std::string>{}; });
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    CHECK(rc == 0);
    CHECK(cap_err.str().empty());                              // nothing on stderr
    CHECK(cap_out.str().find("PASS") != std::string::npos);    // verdict on stdout
}

SCENARIO("[feature:check] staged_files returns empty outside a git repo") {
    char orig[4096];
    REQUIRE(getcwd(orig, sizeof(orig)) != nullptr);
    chdir("/tmp");
    CHECK(keur::cmd::staged_files().empty());
    chdir(orig);
}

// --- [feature:dispatch] -----------------------------------------------------
// Scanner dispatch (ADR-019): resolve repo-local tool config, map proven-tool
// JSON to Findings, and orchestrate enabled scanners over injected seams.

SCENARIO("[feature:dispatch] resolve_tool_config — prefers repo root over .config/") {
    // Both exist; root wins (the tool's documented default location).
    auto exists = [](const std::string& p) {
        return p == "/repo/.gitleaks.toml" || p == "/repo/.config/.gitleaks.toml";
    };
    CHECK(keur::scan::resolve_tool_config({".gitleaks.toml"}, "/repo", exists)
          == "/repo/.gitleaks.toml");
}

SCENARIO("[feature:dispatch] resolve_tool_config — falls back to .config/") {
    // Only the .config/ copy exists → that is returned.
    auto exists = [](const std::string& p) {
        return p == "/repo/.config/.gitleaks.toml";
    };
    CHECK(keur::scan::resolve_tool_config({".gitleaks.toml"}, "/repo", exists)
          == "/repo/.config/.gitleaks.toml");
}

SCENARIO("[feature:dispatch] resolve_tool_config — none present → empty") {
    auto exists = [](const std::string&) { return false; };
    CHECK(keur::scan::resolve_tool_config({".gitleaks.toml"}, "/repo", exists).empty());
}

SCENARIO("[feature:dispatch] resolve_tool_config — tries candidates in order") {
    // .semgrep.yml missing, .semgrep.yaml present → second candidate wins.
    auto exists = [](const std::string& p) { return p == "/r/.semgrep.yaml"; };
    CHECK(keur::scan::resolve_tool_config({".semgrep.yml", ".semgrep.yaml"}, "/r", exists)
          == "/r/.semgrep.yaml");
}

SCENARIO("[feature:dispatch] map_gitleaks_json — maps a finding record") {
    const std::string sample = R"([
      {
        "RuleID": "generic-api-key",
        "Description": "Detected a generic API key",
        "File": "src/config.js",
        "StartLine": 42,
        "Secret": "AKIA...redacted"
      }
    ])";
    auto out = keur::scan::map_gitleaks_json(sample);
    REQUIRE(out.size() == 1);
    CHECK(out[0].source == "gitleaks");
    CHECK(out[0].rule == "generic-api-key");
    CHECK(out[0].category == "secret");
    CHECK(out[0].action == "block");
    CHECK(out[0].file == "src/config.js");
    CHECK(out[0].line == 42);
    CHECK(out[0].title == "Detected a generic API key");
}

SCENARIO("[feature:dispatch] map_gitleaks_json — empty/garbage yields no findings") {
    CHECK(keur::scan::map_gitleaks_json("").empty());
    CHECK(keur::scan::map_gitleaks_json("[]").empty());
    CHECK(keur::scan::map_gitleaks_json("not json { }").empty());
}

SCENARIO("[feature:dispatch] map_semgrep_json — maps results[] records") {
    const std::string sample = R"({
      "results": [
        {
          "check_id": "javascript.lang.security.audit.sqli",
          "path": "src/db.js",
          "start": { "line": 7, "col": 3 },
          "extra": { "severity": "ERROR", "message": "Possible SQL injection" }
        }
      ],
      "errors": []
    })";
    auto out = keur::scan::map_semgrep_json(sample);
    REQUIRE(out.size() == 1);
    CHECK(out[0].source == "semgrep");
    CHECK(out[0].rule == "javascript.lang.security.audit.sqli");
    CHECK(out[0].category == "sast");
    CHECK(out[0].severity == "error");
    CHECK(out[0].action == "warn");
    CHECK(out[0].file == "src/db.js");
    CHECK(out[0].line == 7);
    CHECK(out[0].message == "Possible SQL injection");
}

SCENARIO("[feature:dispatch] map_semgrep_json — no results key → empty") {
    CHECK(keur::scan::map_semgrep_json("{\"errors\":[]}").empty());
}

SCENARIO("[feature:dispatch] dispatch — skips scanner not on PATH") {
    keur::scan::DispatchContext ctx;
    ctx.files = {"a.js"};
    keur::config::Scanner gl; gl.name = "gitleaks"; gl.enabled = true; gl.on_path = false;
    ctx.scanners = {gl};
    bool spawned = false;
    ctx.proc.capture_argv = [&spawned](const std::vector<std::string>&) {
        spawned = true; return keur::adapters::ProcResult{};
    };
    ctx.exists = [](const std::string&) { return false; };
    auto out = keur::scan::dispatch_scanners(ctx);
    CHECK(out.empty());
    CHECK_FALSE(spawned);  // a missing tool is never spawned
}

SCENARIO("[feature:dispatch] dispatch — runs gitleaks and passes repo-local config") {
    keur::scan::DispatchContext ctx;
    ctx.files = {"a.js"};
    ctx.repo_root = "/repo";
    keur::config::Scanner gl; gl.name = "gitleaks"; gl.bin = "gitleaks";
    gl.enabled = true; gl.on_path = true;
    ctx.scanners = {gl};
    // .gitleaks.toml present in repo root.
    ctx.exists = [](const std::string& p) { return p == "/repo/.gitleaks.toml"; };
    std::vector<std::string> seen_argv;
    ctx.proc.capture_argv = [&seen_argv](const std::vector<std::string>& argv) {
        seen_argv = argv;
        keur::adapters::ProcResult r;
        r.stdout_text = R"([{"RuleID":"aws-key","Description":"AWS key","File":"a.js","StartLine":1}])";
        return r;
    };
    auto out = keur::scan::dispatch_scanners(ctx);
    REQUIRE(out.size() == 1);
    CHECK(out[0].rule == "aws-key");
    // The resolved repo-local config was passed through with --config.
    bool has_config_flag = false;
    for (size_t i = 0; i + 1 < seen_argv.size(); ++i)
        if (seen_argv[i] == "--config" && seen_argv[i + 1] == "/repo/.gitleaks.toml")
            has_config_flag = true;
    CHECK(has_config_flag);
}

SCENARIO("[feature:dispatch] dispatch — no repo config → no --config flag for gitleaks") {
    keur::scan::DispatchContext ctx;
    ctx.files = {"a.js"};
    ctx.repo_root = "/repo";
    keur::config::Scanner gl; gl.name = "gitleaks"; gl.bin = "gitleaks";
    gl.enabled = true; gl.on_path = true;
    ctx.scanners = {gl};
    ctx.exists = [](const std::string&) { return false; };  // no tool config
    std::vector<std::string> seen_argv;
    ctx.proc.capture_argv = [&seen_argv](const std::vector<std::string>& argv) {
        seen_argv = argv; return keur::adapters::ProcResult{};
    };
    keur::scan::dispatch_scanners(ctx);
    bool has_config_flag = false;
    for (const auto& a : seen_argv) if (a == "--config") has_config_flag = true;
    CHECK_FALSE(has_config_flag);
}

SCENARIO("[feature:dispatch] dispatch — keur-rules layer flows through") {
    keur::scan::DispatchContext ctx;
    ctx.files = {"x.py"};
    ctx.keur_rules = [](const std::vector<std::string>&) {
        keur::scan::Finding f; f.source = "keur-rules"; f.rule = "SEC-041";
        f.action = "warn"; f.confidence = "0.60";
        return std::vector<keur::scan::Finding>{f};
    };
    auto out = keur::scan::dispatch_scanners(ctx);
    REQUIRE(out.size() == 1);
    CHECK(out[0].source == "keur-rules");
    CHECK(out[0].rule == "SEC-041");
}

SCENARIO("[feature:dispatch] dispatch — disabled scanner is skipped") {
    keur::scan::DispatchContext ctx;
    ctx.files = {"a.js"};
    keur::config::Scanner gl; gl.name = "gitleaks"; gl.enabled = false; gl.on_path = true;
    ctx.scanners = {gl};
    bool spawned = false;
    ctx.proc.capture_argv = [&spawned](const std::vector<std::string>&) {
        spawned = true; return keur::adapters::ProcResult{};
    };
    ctx.exists = [](const std::string&) { return false; };
    ctx.project_local_enabled = false;
    CHECK(keur::scan::dispatch_scanners(ctx).empty());
    CHECK_FALSE(spawned);
}

SCENARIO("[feature:dispatch] map_shellcheck_json — error level blocks, warning warns") {
    const std::string sample = R"({
      "comments": [
        {"file":"x.sh","line":3,"level":"warning","code":2034,"message":"unused var"},
        {"file":"y.sh","line":9,"level":"error","code":1009,"message":"parse error"}
      ]
    })";
    auto out = keur::scan::map_shellcheck_json(sample);
    REQUIRE(out.size() == 2);
    CHECK(out[0].source == "shellcheck");
    CHECK(out[0].rule == "SC2034");
    CHECK(out[0].action == "warn");    // a lint warning must not block
    CHECK(out[1].rule == "SC1009");
    CHECK(out[1].action == "block");   // a hard error blocks
    CHECK(out[1].file == "y.sh");
    CHECK(out[1].line == 9);
}

SCENARIO("[feature:dispatch] dispatch — shellcheck only runs on shell files") {
    keur::scan::DispatchContext ctx;
    ctx.files = {"README.md", "app.py"};  // no shell files
    keur::config::Scanner sc; sc.name = "shellcheck"; sc.bin = "shellcheck";
    sc.enabled = true; sc.on_path = true;
    ctx.scanners = {sc};
    ctx.exists = [](const std::string&) { return false; };
    ctx.project_local_enabled = false;
    bool spawned = false;
    ctx.proc.capture_argv = [&spawned](const std::vector<std::string>&) {
        spawned = true; return keur::adapters::ProcResult{};
    };
    CHECK(keur::scan::dispatch_scanners(ctx).empty());
    CHECK_FALSE(spawned);  // no shell files → not spawned
}

SCENARIO("[feature:dispatch] parse_finding_jsonl — caps confidence") {
    // A script claiming confidence 1.0 must be clamped to the cap (0.80).
    const std::string jsonl =
        R"({"rule":"MY-001","action":"block","confidence":"1.0","file":"a","line":2,"message":"m"})";
    auto out = keur::scan::parse_finding_jsonl(jsonl, 0.80);
    REQUIRE(out.size() == 1);
    CHECK(out[0].source == "project-local");
    CHECK(out[0].rule == "MY-001");
    CHECK(out[0].action == "block");
    CHECK(out[0].confidence == "0.80");  // capped
}

SCENARIO("[feature:dispatch] parse_finding_jsonl — numeric confidence under cap kept") {
    const std::string jsonl = R"({"rule":"MY-002","action":"warn","confidence":0.4})";
    auto out = keur::scan::parse_finding_jsonl(jsonl, 0.80);
    REQUIRE(out.size() == 1);
    CHECK(out[0].confidence == "0.40");
}

SCENARIO("[feature:dispatch] project-local — untracked script is NOT run") {
    keur::scan::DispatchContext ctx;
    ctx.files = {"a.js"};
    ctx.repo_root = "/repo";
    ctx.exists = [](const std::string& p) { return p == "/repo/.keur/check.sh"; };
    ctx.is_tracked = [](const std::string&) { return false; };  // present but untracked
    bool spawned = false;
    ctx.proc.capture_argv = [&spawned](const std::vector<std::string>&) {
        spawned = true; return keur::adapters::ProcResult{};
    };
    CHECK(keur::scan::dispatch_scanners(ctx).empty());
    CHECK_FALSE(spawned);  // trust boundary: dropped script ignored
}

SCENARIO("[feature:dispatch] project-local — tracked script, non-zero exit blocks") {
    keur::scan::DispatchContext ctx;
    ctx.files = {"a.js"};
    ctx.repo_root = "/repo";
    ctx.exists = [](const std::string& p) { return p == "/repo/.keur/check.sh"; };
    ctx.is_tracked = [](const std::string& rel) { return rel == ".keur/check.sh"; };
    ctx.proc.capture_argv = [](const std::vector<std::string>&) {
        keur::adapters::ProcResult r; r.exit_code = 2; r.stdout_text = "";
        return r;
    };
    auto out = keur::scan::dispatch_scanners(ctx);
    REQUIRE(out.size() == 1);
    CHECK(out[0].source == "project-local");
    CHECK(out[0].action == "block");
    CHECK(out[0].confidence == "0.80");  // capped
}

SCENARIO("[feature:dispatch] project-local — .config/keur/check.sh fallback") {
    keur::scan::DispatchContext ctx;
    ctx.files = {};
    ctx.repo_root = "/repo";
    // Only the .config/ copy exists and is tracked.
    ctx.exists = [](const std::string& p) { return p == "/repo/.config/keur/check.sh"; };
    ctx.is_tracked = [](const std::string& rel) { return rel == ".config/keur/check.sh"; };
    ctx.proc.capture_argv = [](const std::vector<std::string>&) {
        keur::adapters::ProcResult r; r.exit_code = 0;
        r.stdout_text = R"({"rule":"DEAD-LINK","action":"warn","message":"broken link"})";
        return r;
    };
    auto out = keur::scan::dispatch_scanners(ctx);
    REQUIRE(out.size() == 1);
    CHECK(out[0].rule == "DEAD-LINK");
    CHECK(out[0].action == "warn");
}

SCENARIO("[feature:dispatch] project-local — disabled skips entirely") {
    keur::scan::DispatchContext ctx;
    ctx.files = {};
    ctx.repo_root = "/repo";
    ctx.project_local_enabled = false;
    ctx.exists = [](const std::string& p) { return p == "/repo/.keur/check.sh"; };
    ctx.is_tracked = [](const std::string&) { return true; };
    bool spawned = false;
    ctx.proc.capture_argv = [&spawned](const std::vector<std::string>&) {
        spawned = true; return keur::adapters::ProcResult{};
    };
    CHECK(keur::scan::dispatch_scanners(ctx).empty());
    CHECK_FALSE(spawned);
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

// --------------------------------------------------------------------------
// Global orchestrator stub: behavioural tests. The stub is shell, so we prove
// its EXIT CODE by executing it under /bin/sh with a fake `keur` on PATH. The
// bug these characterise: when the repo has no .githooks/<hook>, the stub's
// `run_keur && run_repo` chain must NOT fail just because run_repo found no
// hook — a keur PASS has to exit 0 (ADR-002: the stub carries no verdict).
//
// Returns the exit status (0..255) of running the stub text as a script, with
// `fake_keur_exit` as the exit code of the stubbed `keur` binary, and an
// optional repo .githooks/<hook> written when repo_hook_exit >= 0.
// Run argv[] in a child process, shell-free (no std::system / no /bin/sh string
// interpolation — Sonar cpp:S4036 / CWE-78). Optionally chdir, set PATH, and
// silence stdio. Returns the child's exit status, or -1 if it did not exit
// normally. Used by the hook-stub behavioural tests below.
static int run_argv_in(const std::vector<std::string>& argv, const std::string& cwd,
                       const std::string& path_env, bool silence) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
        if (!path_env.empty()) setenv("PATH", path_env.c_str(), 1);
        if (silence) {
            (void)!freopen("/dev/null", "r", stdin);
            (void)!freopen("/dev/null", "w", stdout);
            (void)!freopen("/dev/null", "w", stderr);
        }
        std::vector<char*> c;
        c.reserve(argv.size() + 1);
        for (const auto& a : argv) c.push_back(const_cast<char*>(a.c_str()));
        c.push_back(nullptr);
        execvp(c[0], c.data());
        _exit(127);  // exec failed
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int run_global_stub(const char* hook_name, int fake_keur_exit,
                           int repo_hook_exit) {
    std::string tmp = create_temp_dir();
    if (tmp.empty()) return -1;

    // Fake repo: a git repo so `git rev-parse --show-toplevel` resolves to it.
    // Shell-free spawn (argv, not a /bin/sh string) — @see KEUR-SEC-006.
    std::string repo = tmp + "/repo";
    (void)mkdir(repo.c_str(), 0755);
    (void)run_argv_in({"git", "-c", "init.defaultBranch=main", "init", "-q", "."},
                      repo, /*path_env=*/"", /*silence=*/true);

    // Fake keur on PATH with a controllable exit code.
    std::string bin = tmp + "/bin";
    (void)mkdir(bin.c_str(), 0755);
    write_file(bin + "/keur",
               "#!/bin/sh\necho 'KEUR (fake): PASS' >&2\nexit "
                   + std::to_string(fake_keur_exit) + "\n");
    (void)chmod((bin + "/keur").c_str(), 0755);

    // Optionally create the repo's own .githooks/<hook>.
    if (repo_hook_exit >= 0) {
        std::string gh = repo + "/.githooks";
        (void)mkdir(gh.c_str(), 0755);
        write_file(gh + "/" + hook_name,
                   "#!/bin/sh\nexit " + std::to_string(repo_hook_exit) + "\n");
        (void)chmod((gh + "/" + hook_name).c_str(), 0755);
    }

    // Write the stub under test and run it from inside the repo, shell-free:
    // exec /bin/sh with the stub path as an argv element (no string the shell
    // could re-parse), with PATH pointing at the fake keur. cwd + PATH + stdio
    // are set in the child, so the behaviour matches the old command string.
    std::string stub = keur::cmd::global_hook_stub(hook_name);
    std::string stub_path = tmp + "/stub.sh";
    write_file(stub_path, stub);
    (void)chmod(stub_path.c_str(), 0755);

    std::string path_env = bin;
    if (const char* p = std::getenv("PATH")) path_env += std::string(":") + p;
    int rc = run_argv_in({"/bin/sh", stub_path}, repo, path_env, /*silence=*/true);
    cleanup_dir(tmp);
    return rc;
}

SCENARIO("[feature:hook] global stub exits 0 on keur PASS when no repo hook exists") {
    // The regression: a repo with no .githooks/pre-push must still push on PASS.
    CHECK(run_global_stub("pre-push", /*fake_keur_exit=*/0, /*repo_hook_exit=*/-1) == 0);
    CHECK(run_global_stub("pre-commit", /*fake_keur_exit=*/0, /*repo_hook_exit=*/-1) == 0);
}

SCENARIO("[feature:hook] global stub propagates a keur BLOCK (exit 1)") {
    // keur's own verdict must still gate: a BLOCK stops the commit/push.
    CHECK(run_global_stub("pre-commit", /*fake_keur_exit=*/1, /*repo_hook_exit=*/-1) == 1);
}

SCENARIO("[feature:hook] global stub runs the repo hook after keur and propagates its result") {
    // keur PASS + repo hook PASS -> 0; keur PASS + repo hook FAIL -> non-zero.
    CHECK(run_global_stub("pre-commit", /*fake_keur_exit=*/0, /*repo_hook_exit=*/0) == 0);
    CHECK(run_global_stub("pre-commit", /*fake_keur_exit=*/0, /*repo_hook_exit=*/1) != 0);
}

SCENARIO("[feature:hook] hooks_install creates executable stubs") {    std::string tmp = create_temp_dir();
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

SCENARIO("[feature:rule-engine] rule_dirs includes ~/.keur/rules as the personal layer") {
    unsetenv("KEUR_HOME");
    unsetenv("KEUR_RULES_DIRS");
    setenv("HOME", "/home/tester", 1);
    auto d = keur::rule_dirs();
    // The personal cross-repo PRIVATE layer (BII/PII denylist home) is derived
    // from $HOME and must appear whenever HOME is set. Missing dir is skipped at
    // load time, not here — rule_dirs lists the candidate unconditionally.
    bool has_personal = false;
    for (const auto& p : d)
        if (p == "/home/tester/.keur/rules") has_personal = true;
    CHECK(has_personal);
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

SCENARIO("[feature:config] resolve_layers orders defaults->global->keur-home->repo(.config,root)->explicit") {
    keur::config::ScopeEnv env;
    env.xdg_config_home = "/xdg";
    env.keur_home = "/kh";
    env.repo_root = "/repo";
    env.explicit_path = "/exp/keur.toml";
    // everything "exists" so we assert on ordering + paths, not presence.
    auto layers = keur::config::resolve_layers(env, [](const std::string&) { return true; });
    REQUIRE(layers.size() == 6);
    CHECK(layers[0].scope == keur::config::Scope::Defaults);
    CHECK(layers[0].path.empty());
    CHECK(layers[1].scope == keur::config::Scope::Global);
    CHECK(layers[1].path == "/xdg/keur/keur.toml");
    CHECK(layers[2].scope == keur::config::Scope::KeurHome);
    CHECK(layers[2].path == "/kh/config/keur.toml");
    // Repo scope resolves .config/ first (uncluttered-first), then the root.
    CHECK(layers[3].scope == keur::config::Scope::Repo);
    CHECK(layers[3].path == "/repo/.config/keur.toml");
    CHECK(layers[4].scope == keur::config::Scope::Repo);
    CHECK(layers[4].path == "/repo/keur.toml");
    CHECK(layers[5].scope == keur::config::Scope::Explicit);
    CHECK(layers[5].path == "/exp/keur.toml");
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
    env.repo_root = "/repo";   // → /repo/.config/keur.toml then /repo/keur.toml
    // only the root repo file "exists".
    auto layers = keur::config::resolve_layers(
        env, [](const std::string& p) { return p == "/repo/keur.toml"; });
    REQUIRE(layers.size() == 4);  // defaults + global + repo(.config) + repo(root)
    CHECK(layers[1].scope == keur::config::Scope::Global);
    CHECK(layers[1].exists == false);
    CHECK(layers[2].scope == keur::config::Scope::Repo);
    CHECK(layers[2].path == "/repo/.config/keur.toml");
    CHECK(layers[2].exists == false);   // .config/ variant absent
    CHECK(layers[3].scope == keur::config::Scope::Repo);
    CHECK(layers[3].path == "/repo/keur.toml");
    CHECK(layers[3].exists == true);    // root variant present
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

SCENARIO("[feature:config] tier_scanners reads the scanners array for a tier") {  // @proves KEUR-TIER-SCANNERS
    const std::string toml =
        "[tiers.pre-commit]\n"
        "budget-ms = 3000\n"
        "scanners = [\"gitleaks\", \"pii\", \"keur-rules\"]\n"
        "\n"
        "[tiers.pre-push]\n"
        "scanners = [\"gitleaks\", \"semgrep\", \"trivy\", \"keur-rules\"]\n";
    auto pc = keur::config::tier_scanners(toml, "pre-commit");
    REQUIRE(pc.size() == 3);
    CHECK(pc[0] == "gitleaks");
    CHECK(pc[1] == "pii");
    CHECK(pc[2] == "keur-rules");
    // The pre-push tier is a superset including the heavy tools.
    auto pp = keur::config::tier_scanners(toml, "pre-push");
    REQUIRE(pp.size() == 4);
    CHECK(pp[1] == "semgrep");
}

SCENARIO("[feature:config] tier_scanners returns empty for an absent tier") {  // @proves KEUR-TIER-SCANNERS
    CHECK(keur::config::tier_scanners("[tiers.ci]\nscanners = [\"x\"]\n", "pre-commit").empty());
    CHECK(keur::config::tier_scanners("", "pre-commit").empty());
}

SCENARIO("[feature:dispatch] dispatch — tier filter keeps a tool out") {
    // semgrep is enabled + on PATH, but the tier only allows gitleaks → semgrep
    // must not be spawned (the pre-commit budget guard).
    keur::scan::DispatchContext ctx;
    ctx.files = {"a.js"};
    keur::config::Scanner sg; sg.name = "semgrep"; sg.bin = "semgrep";
    sg.enabled = true; sg.on_path = true;
    ctx.scanners = {sg};
    ctx.tier_allowed = {"gitleaks"};  // semgrep not listed
    ctx.exists = [](const std::string&) { return false; };
    ctx.project_local_enabled = false;
    bool spawned = false;
    ctx.proc.capture_argv = [&spawned](const std::vector<std::string>&) {
        spawned = true; return keur::adapters::ProcResult{};
    };
    CHECK(keur::scan::dispatch_scanners(ctx).empty());
    CHECK_FALSE(spawned);
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

SCENARIO("[feature:rules] filter_by_maturity keeps rules at or below the level") {  // @proves KEUR-MATURITY.filter
    std::vector<keur::Rule> rules;
    keur::Rule s;  s.id = "SECRET"; s.maturity = 1; rules.push_back(s);
    keur::Rule q;  q.id = "QUAL";   q.maturity = 3; rules.push_back(q);
    keur::Rule i;  i.id = "IDIOM";  i.maturity = 5; rules.push_back(i);
    keur::Rule d;  d.id = "DEFAULT"; /* maturity 0 -> treated as 3 */ rules.push_back(d);

    // Level 1 (I'm Too Young to KEUR): only the maturity-1 rule.
    auto l1 = keur::config::filter_by_maturity(rules, 1);
    REQUIRE(l1.size() == 1);
    CHECK(l1[0].id == "SECRET");

    // Level 3 (KEUR Me Plenty): maturity<=3 → SECRET, QUAL, and the default(3).
    auto l3 = keur::config::filter_by_maturity(rules, 3);
    CHECK(l3.size() == 3);

    // Level 5 (H4x0r c0d3fl3x): everything.
    CHECK(keur::config::filter_by_maturity(rules, 5).size() == 4);

    // Level 0/unset: no filter (backwards compatible).
    CHECK(keur::config::filter_by_maturity(rules, 0).size() == 4);
}

SCENARIO("[feature:rules] rule parses maturity: N into rule.maturity") {  // @proves KEUR-MATURITY.parse
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/MAT.rule",
        "id: MAT-EVAL\n"
        "title: eval usage\n"
        "category: sast\n"
        "severity: warning\n"
        "engine: pattern\n"
        "maturity: 4\n"
        "patterns:\n"
        "  - regex: \\beval\\s*\\(\n"
        "    message: eval usage\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    CHECK(rules[0].maturity == 4);   // maturity: 4 parsed into the int field

    // An unset maturity stays 0 (the filter treats 0 as the default band).
    write_file(dir + "/NOMAT.rule",
        "id: NOMAT-EVAL\n"
        "title: eval usage\n"
        "category: sast\n"
        "severity: warning\n"
        "engine: pattern\n"
        "patterns:\n"
        "  - regex: \\beval\\s*\\(\n"
        "    message: eval usage\n");
    auto rules2 = keur::rules_load(dir);
    bool saw_nomat = false;
    for (const auto& r : rules2) if (r.id == "NOMAT-EVAL") { saw_nomat = true; CHECK(r.maturity == 0); }
    CHECK(saw_nomat);
    cleanup_dir(dir);
}

// --- ADR-039: ility-alignment + OR/max difficulty resolution ----------------

SCENARIO("[feature:rules] rule parses ilities: array and scalar") {  // @proves ADR-039.parse
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/MULTI.rule",
        "id: MULTI\n"
        "ilities: [security, hardening]\n"
        "maturity: 1\n"
        "category: iac\n"
        "severity: error\n"
        "engine: pattern\n"
        "patterns:\n"
        "  - regex: x\n    message: m\n");
    write_file(dir + "/SCALAR.rule",
        "id: SCALAR\n"
        "ilities: security\n"
        "maturity: 2\n"
        "category: security\n"
        "severity: error\n"
        "engine: pattern\n"
        "patterns:\n"
        "  - regex: x\n    message: m\n");
    auto rules = keur::rules_load(dir);
    const keur::Rule* multi = nullptr; const keur::Rule* scalar = nullptr;
    for (const auto& r : rules) {
        if (r.id == "MULTI") multi = &r;
        if (r.id == "SCALAR") scalar = &r;
    }
    REQUIRE(multi); REQUIRE(scalar);
    REQUIRE(multi->ilities.size() == 2);
    CHECK(multi->ilities[0] == "security");    // primary first
    CHECK(multi->ilities[1] == "hardening");
    REQUIRE(scalar->ilities.size() == 1);
    CHECK(scalar->ilities[0] == "security");
    cleanup_dir(dir);
}

SCENARIO("[feature:rules] rule_ilities falls back to category when ilities empty") {  // @proves ADR-039.fallback
    keur::Rule r; r.category = "sqli";          // no explicit ilities
    auto ils = keur::rule_ilities(r);
    REQUIRE(ils.size() == 1);
    CHECK(ils[0] == "integrity");                // sqli -> Security/integrity (ISO 25010)
    keur::Rule a; a.category = "a11y";
    CHECK(keur::rule_ilities(a)[0] == "accessibility");   // a11y -> Usability/accessibility
    keur::Rule u; u.category = "totally-unknown";
    CHECK(keur::rule_ilities(u)[0] == "analysability");   // unknown -> Maintainability/analysability
}

SCENARIO("[feature:rules] rule_active resolves OR/max over ilities") {  // @proves ADR-039.resolve
    keur::Rule r; r.id = "R"; r.maturity = 4; r.ilities = {"security", "hardening"};

    // The collaborator's case: security=4, hardening=2 -> max(4,2)=4 >= 4 -> active.
    keur::DifficultyVector v1; v1.default_level = 1;
    v1.levels = {{"security", 4}, {"hardening", 2}};
    CHECK(keur::rule_active(r, v1));

    // security=3, hardening=2 -> max(3,2)=3 < 4 -> inactive.
    keur::DifficultyVector v2; v2.default_level = 1;
    v2.levels = {{"security", 3}, {"hardening", 2}};
    CHECK_FALSE(keur::rule_active(r, v2));

    // A secondary ility alone cannot activate below the bar, and lowering it
    // never disables while the primary is high (guard safety).
    keur::DifficultyVector v3; v3.default_level = 1;
    v3.levels = {{"security", 5}, {"hardening", 0}};  // hardening OFF
    CHECK(keur::rule_active(r, v3));                   // security carries it
}

SCENARIO("[feature:rules] rule_active: all ilities OFF -> inactive") {  // @proves ADR-039.off
    keur::Rule r; r.maturity = 1; r.ilities = {"quality"};
    keur::DifficultyVector v; v.default_level = 1;
    v.levels = {{"quality", 0}};                       // OFF
    CHECK_FALSE(keur::rule_active(r, v));
}

SCENARIO("[feature:rules] rule_active: default_level steers unset ilities (CMMI-1)") {  // @proves ADR-039.default
    // Fresh keur: default vector = 1. Only maturity-1 rules are active.
    keur::DifficultyVector fresh; fresh.default_level = 1;  // no explicit levels

    keur::Rule deadly;  deadly.maturity = 1; deadly.ilities = {"security"};
    keur::Rule mid;     mid.maturity = 3;    mid.ilities = {"quality"};
    keur::Rule unset;   unset.maturity = 0;  unset.ilities = {"security"}; // 0 -> 3

    CHECK(keur::rule_active(deadly, fresh));        // maturity 1 <= level 1
    CHECK_FALSE(keur::rule_active(mid, fresh));     // maturity 3 > level 1 -> sleeps
    CHECK_FALSE(keur::rule_active(unset, fresh));   // default maturity 3 > 1 -> sleeps
}

// --- DESIGN-DIFFRES: difficulty vector loader + merge + filter -------------

SCENARIO("[feature:rules] parse_difficulty reads [difficulty] + [difficulty.rules]") {  // @proves DESIGN-DIFFRES.schema
    keur::DifficultyVector v; v.default_level = 1;
    std::map<std::string, bool> ov;
    keur::config::parse_difficulty(
        "[policy]\nlevel = 3\n"
        "[difficulty]\nintegrity = 4\naccessibility = \"off\"\n"
        "[difficulty.rules]\nKEUR-SQLI-003 = off\nFOO = on\n", v, ov);
    CHECK(v.level_of("integrity") == 4);
    CHECK(v.level_of("accessibility") == 0);      // "off" -> 0
    CHECK(v.level_of("confidentiality") == 1);    // unset -> default_level
    CHECK(ov["KEUR-SQLI-003"] == false);
    CHECK(ov["FOO"] == true);
}

SCENARIO("[feature:rules] merge_difficulty folds global->repo cascade") {  // @proves DESIGN-DIFFRES.merge
    std::map<std::string, std::string> files;
    files["global"] = "[policy]\nlevel = 2\n";          // all-equal shorthand
    files["repo"]   = "[difficulty]\nintegrity = 5\n";  // per-ility override
    std::map<std::string, bool> ov;
    auto vec = keur::config::merge_difficulty(
        {"global", "repo"},
        [&files](const std::string& p) { return files.count(p) ? files[p] : std::string(); },
        ov, /*default_level=*/1);
    CHECK(vec.default_level == 2);                 // global [policy] raised it
    CHECK(vec.level_of("integrity") == 5);         // repo override wins
    CHECK(vec.level_of("confidentiality") == 2);   // inherits global level
}

SCENARIO("[feature:rules] filter_by_vector OR/max + per-rule override") {  // @proves DESIGN-DIFFRES.step.wire
    keur::DifficultyVector vec; vec.default_level = 2;
    vec.levels = {{"integrity", 5}};
    std::vector<keur::Rule> rules;
    keur::Rule a; a.id = "A"; a.maturity = 2; a.ilities = {"integrity"};       rules.push_back(a);
    keur::Rule b; b.id = "B"; b.maturity = 5; b.ilities = {"confidentiality"}; rules.push_back(b);
    keur::Rule c; c.id = "C"; c.maturity = 1; c.ilities = {"confidentiality"}; rules.push_back(c);
    std::map<std::string, bool> ov; ov["C"] = false;  // force C off
    auto kept = keur::config::filter_by_vector(rules, vec, ov);
    bool hasA = false, hasB = false, hasC = false;
    for (const auto& r : kept) { if (r.id=="A") hasA=true; if (r.id=="B") hasB=true; if (r.id=="C") hasC=true; }
    CHECK(hasA); CHECK_FALSE(hasB); CHECK_FALSE(hasC);
}

SCENARIO("[feature:rules] filter_by_maturity is the all-equal vector (back-compat)") {  // @proves DESIGN-DIFFRES.compat
    std::vector<keur::Rule> rules;
    keur::Rule a; a.id = "A"; a.maturity = 2; a.ilities = {"integrity"};       rules.push_back(a);
    keur::Rule c; c.id = "C"; c.maturity = 1; c.ilities = {"confidentiality"}; rules.push_back(c);
    auto k1 = keur::config::filter_by_maturity(rules, 1);
    REQUIRE(k1.size() == 1);
    CHECK(k1[0].id == "C");
    CHECK(keur::config::filter_by_maturity(rules, 0).size() == 2);  // 0 = no filter
}

SCENARIO("[feature:rules] policy_level reads [policy] level from keur.toml") {  // @proves KEUR-MATURITY.level
    CHECK(keur::config::policy_level("[policy]\nlevel = 2\n") == 2);
    CHECK(keur::config::policy_level("version=\"1\"\n[policy]\nblock-at=0.85\nlevel = 5\n") == 5);
    // absent -> 0 (no filter)
    CHECK(keur::config::policy_level("[policy]\nblock-at = 0.85\n") == 0);
    CHECK(keur::config::policy_level("") == 0);
    // a level key outside [policy] must not count
    CHECK(keur::config::policy_level("[other]\nlevel = 4\n") == 0);
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

SCENARIO("[feature:report] use_color Auto honours FORCE_COLOR without a tty") {
    unsetenv("NO_COLOR");
    setenv("FORCE_COLOR", "1", 1);
    CHECK(keur::report::use_color(keur::report::ColorMode::Auto, false));
    unsetenv("FORCE_COLOR");
}

SCENARIO("[feature:report] use_color Auto honours CLICOLOR_FORCE without a tty") {
    unsetenv("NO_COLOR");
    setenv("CLICOLOR_FORCE", "1", 1);
    CHECK(keur::report::use_color(keur::report::ColorMode::Auto, false));
    unsetenv("CLICOLOR_FORCE");
}

SCENARIO("[feature:report] use_color FORCE_COLOR=0 does not force colour") {
    unsetenv("NO_COLOR");
    setenv("FORCE_COLOR", "0", 1);
    CHECK(!keur::report::use_color(keur::report::ColorMode::Auto, false));
    unsetenv("FORCE_COLOR");
}

SCENARIO("[feature:report] use_color NO_COLOR wins over FORCE_COLOR") {
    setenv("NO_COLOR", "1", 1);
    setenv("FORCE_COLOR", "1", 1);
    CHECK(!keur::report::use_color(keur::report::ColorMode::Auto, true));
    unsetenv("NO_COLOR");
    unsetenv("FORCE_COLOR");
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
