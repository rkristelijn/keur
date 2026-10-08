// keur:ignore-file SEC-041 — test source: contains patterns it checks for
/**
 * @file tests/integration_test.cpp
 * @brief Integration tests — the real rule engine over real fixture files
 *        (doctest suite "integration"). Split out of keur_test.cpp (ADR-002).
 *
 * No doctest main here (that is in test_main.cpp); this TU only registers its
 * scenarios. Shared helpers come from test_helpers.h; this file keeps them
 * unqualified via `using`.
 */
#include <sys/stat.h>

#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "../../../vendor/doctest.h"
#include "rules/rule_engine.h"
#include "cmd/check.h"
#include "cmd/hook.h"
#include "cmd/scan.h"
#include "test_helpers.h"

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

SCENARIO("[feature:rule-engine] KEUR-QUAL-014 fires on a bash script without strict mode") {  // @proves KEUR-QUAL-014
    auto rules = keur::rules_load(keur_rules_dir() + "/quality");
    REQUIRE(!rules.empty());
    std::string dir = create_temp_dir();
    // No `set -euo pipefail` → absence rule fires.
    write_file(dir + "/no_strict.sh", "#!/usr/bin/env bash\necho hi\n");
    auto miss = keur::rules_scan_files(rules, {dir + "/no_strict.sh"});
    CHECK(has_rule(miss, "KEUR-QUAL-014"));
    // Strict mode present → no finding.
    write_file(dir + "/has_strict.sh", "#!/usr/bin/env bash\nset -euo pipefail\necho hi\n");
    auto ok = keur::rules_scan_files(rules, {dir + "/has_strict.sh"});
    CHECK(!has_rule(ok, "KEUR-QUAL-014"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] KEUR-QUAL-012 fires on curl without a timeout") {  // @proves KEUR-QUAL-012
    auto rules = keur::rules_load(keur_rules_dir() + "/quality");
    REQUIRE(!rules.empty());
    std::string dir = create_temp_dir();
    write_file(dir + "/no_timeout.sh", "#!/usr/bin/env bash\ncurl https://example.com/x\n");
    auto miss = keur::rules_scan_files(rules, {dir + "/no_timeout.sh"});
    CHECK(has_rule(miss, "KEUR-QUAL-012"));
    // A timeout flag on the same line suppresses via exclude:.
    write_file(dir + "/has_timeout.sh", "#!/usr/bin/env bash\ncurl --max-time 30 https://example.com/x\n");
    auto ok = keur::rules_scan_files(rules, {dir + "/has_timeout.sh"});
    CHECK(!has_rule(ok, "KEUR-QUAL-012"));
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
    mkdir(layer.c_str(), 0700);
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
    mkdir(layer.c_str(), 0700);
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
    mkdir(layer.c_str(), 0700);
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

// --- base64-unwrap engine (ADR-049) -----------------------------------------
// The engine decodes ONE base64 layer and re-runs the rule's OWN patterns over
// the decoded text. Detection lives in the rule (data); the engine only does the
// decode transform (ADR-009). The finding points at the ENCODED line and never
// echoes the decoded secret.

namespace {
// A base64-unwrap rule whose inner patterns look for credential shapes in the
// DECODED text. Written to `dir` and loaded; returns the loaded rule set.
std::vector<keur::Rule> load_b64_unwrap_rule(const std::string& dir) {
    write_file(dir + "/B64.rule",
        "id: TEST-B64-001\n"
        "title: secret hidden in base64\n"
        "category: secret\n"
        "severity: error\n"
        "engine: base64-unwrap\n"
        "min_count: 16\n"
        "patterns:\n"
        "  - regex: (?i)[\"']?(password|secret|token|api[_-]?key)[\"']?\\s*:\\s*[\"'][^\"']{8,}[\"']\n"
        "    message: credential-shaped value one base64 layer deep\n"
        "  - regex: AKIA[0-9A-Z]{16}\n"
        "    message: AWS key revealed after decode\n");
    return keur::rules_load(dir);
}
}  // namespace

SCENARIO("[feature:rule-engine] base64-unwrap fires on a secret hidden one base64 layer deep") {  // @proves ADR-049 engine-base64-unwrap
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    auto rules = load_b64_unwrap_rule(dir);
    REQUIRE(!rules.empty());
    // base64 of {"token":"supersecretvalue"} — the inner shape matches, the
    // encoded blob does not. (Precomputed so the test needs no base64 tool.)
    // echo -n '{"token":"supersecretvalue"}' | base64
    std::string blob = "eyJ0b2tlbiI6InN1cGVyc2VjcmV0dmFsdWUifQ==";
    std::string src = dir + "/userdata.sh";
    write_file(src, "setup --config " + blob + "\n");
    auto fs = keur::rules_scan_files(rules, {src});
    CHECK(has_rule(fs, "TEST-B64-001"));
    // The finding points at the encoded line (line 1), never the decoded value.
    for (const auto& f : fs)
        if (f.rule_id == "TEST-B64-001") {
            CHECK(f.line == 1);
            CHECK(f.message.find("supersecretvalue") == std::string::npos);
        }
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] base64-unwrap surfaces an AWS key after decode") {  // @proves ADR-049
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    auto rules = load_b64_unwrap_rule(dir);
    REQUIRE(!rules.empty());
    // echo -n '{"aws_access_key_id":"AKIA...EXAMPLE"}' | base64  (example key id; keur:ignore SEC-010)
    std::string blob = "eyJhd3NfYWNjZXNzX2tleV9pZCI6IkFLSUFJT1NGT0ROTjdFWEFNUExFIn0=";
    std::string src = dir + "/config.ts";
    write_file(src, "const c = \"" + blob + "\";\n");
    auto fs = keur::rules_scan_files(rules, {src});
    CHECK(has_rule(fs, "TEST-B64-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] base64-unwrap does NOT fire on a harmless base64 blob") {  // @proves ADR-049 (filter)
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    auto rules = load_b64_unwrap_rule(dir);
    REQUIRE(!rules.empty());
    // echo -n 'this is a long but entirely harmless sentence' | base64
    std::string blob = "dGhpcyBpcyBhIGxvbmcgYnV0IGVudGlyZWx5IGhhcm1sZXNzIHNlbnRlbmNl";
    std::string src = dir + "/note.txt";
    write_file(src, "note = \"" + blob + "\"\n");
    auto fs = keur::rules_scan_files(rules, {src});
    CHECK(!has_rule(fs, "TEST-B64-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] base64-unwrap ignores short runs below the length floor") {  // @proves ADR-049 (min_count floor)
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    auto rules = load_b64_unwrap_rule(dir);
    REQUIRE(!rules.empty());
    // "eyJ4Ijox" decodes to {"x":1 but is only 8 chars — below the min_count:16
    // floor, so the engine never decodes it. No finding.
    std::string src = dir + "/tiny.txt";
    write_file(src, "v = eyJ4Ijox\n");
    auto fs = keur::rules_scan_files(rules, {src});
    CHECK(!has_rule(fs, "TEST-B64-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] urldecode-unwrap fires on a percent-encoded secret") {  // @proves ADR-049 engine-urldecode-unwrap
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/URL.rule",
        "id: TEST-URL-001\n"
        "title: secret hidden in percent-encoding\n"
        "category: secret\n"
        "severity: error\n"
        "engine: urldecode-unwrap\n"
        "min_count: 8\n"
        "patterns:\n"
        "  - regex: (?i)(password|secret|token)=[^&]{6,}\n"
        "    message: credential-shaped value one url-encode layer deep\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    // "token=supersecret" percent-encoded as token%3Dsupersecret
    std::string src = dir + "/callback.txt";
    write_file(src, "redirect = /cb?token%3Dsupersecret%26ok\n");
    auto fs = keur::rules_scan_files(rules, {src});
    CHECK(has_rule(fs, "TEST-URL-001"));
    // Plain text with no percent-escape must not fire (nothing to unwrap).
    std::string plain = dir + "/plain.txt";
    write_file(plain, "note = just a normal sentence here\n");
    auto fs2 = keur::rules_scan_files(rules, {plain});
    CHECK(!has_rule(fs2, "TEST-URL-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] hex-unwrap fires on a hex-encoded secret blob") {  // @proves ADR-049 engine-hex-unwrap
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/HEX.rule",
        "id: TEST-HEX-001\n"
        "title: secret hidden in hex\n"
        "category: secret\n"
        "severity: error\n"
        "engine: hex-unwrap\n"
        "min_count: 10\n"
        "patterns:\n"
        "  - regex: (?i)(password|secret|token)=[^;]{4,}\n"
        "    message: credential-shaped value hex-encoded\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());
    // hex of "token=hunter2secret"
    // printf 'token=hunter2secret' | od -An -tx1 | tr -d ' '
    std::string hex = "746f6b656e3d68756e74657232736563726574";
    std::string src = dir + "/blob.txt";
    write_file(src, "data = " + hex + "\n");
    auto fs = keur::rules_scan_files(rules, {src});
    CHECK(has_rule(fs, "TEST-HEX-001"));
    // A random hex hash that decodes to binary must not fire (UTF-8 filter).
    std::string noise = dir + "/hash.txt";
    write_file(noise, "sha = 9f8e7d6c5b4a3f2e1d0c9b8a7766554433221100\n");
    auto fs2 = keur::rules_scan_files(rules, {noise});
    CHECK(!has_rule(fs2, "TEST-HEX-001"));
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
    keur::cmd::CheckOptions opts;
    auto rc = keur::cmd::check(opts, [&path]() {
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
    auto rc = keur::cmd::check(opts, [&path]() {
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

SCENARIO("[feature:rule-engine] skip_comments rule keeps string matches when a skip_strings rule is also loaded") {
    // Regression: the scan used to strip ONE shared variant per file. If any
    // loaded rule wanted skip_strings, every skip_comments rule was matched
    // against the comments+strings-stripped text — so a skip_comments rule whose
    // pattern targets text INSIDE a string literal silently matched nothing.
    // The fix strips comments-only and comments+strings separately and picks the
    // variant per rule. Here: a skip_comments rule (pattern inside a string) must
    // still fire even though a skip_strings rule is loaded alongside it.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    // Rule A: skip_comments only, pattern matches a token that lives in a string.
    write_file(dir + "/A_GLYPH.rule",
        "id: SC-GLYPH\ntitle: glyph escape\ncategory: quality\nseverity: warning\n"
        "engine: pattern\nskip_comments: true\ntarget:\n  extensions: .cpp\n"
        "patterns:\n  - regex: \\\\u[0-9A-Fa-f]{4}\n    message: glyph escape\n");
    // Rule B: skip_strings, forces the comments+strings strip to be computed too.
    write_file(dir + "/B_STR.rule",
        "id: SS-TODO\ntitle: todo\ncategory: quality\nseverity: warning\n"
        "engine: pattern\nskip_strings: true\ntarget:\n  extensions: .cpp\n"
        "patterns:\n  - regex: \\bTODO\\b\n    message: todo\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(rules.size() == 2);

    std::string cpp = dir + "/glyph.cpp";
    // The \u2713 lives inside a string literal, on a line that also has a comment.
    write_file(cpp, "const char* ok = \"\\u2713\";  // check glyph\n");
    auto fs = keur::rules_scan_files(rules, {cpp});
    int glyph_hits = 0;
    for (auto& f : fs) if (f.rule_id == "SC-GLYPH") glyph_hits++;
    CHECK(glyph_hits == 1);   // was 0 before the fix (string content wrongly stripped)
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

SCENARIO("[feature:rule-engine] extract-duplicates flags a repeated capture value") {  // @proves engine-extract-duplicates
    // Per file: apply `extract` (regex with a capture group), collect the
    // captured value, and fire on the SECOND+ occurrence of the same value.
    // Unique values never fire. {match} is substituted into the message.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/DUP.rule",
        "id: DUP-1\ntitle: duplicate test name\ncategory: quality\nseverity: warning\n"
        "engine: extract-duplicates\n"
        "extract: (?:it|test)\\(\\s*['\\\"]([^'\\\"]+)['\\\"]\ncapture: 1\n"
        "target:\n  extensions: .js\n"
        "patterns:\n  - regex: x\n    message: \"Duplicate test name '{match}'\"\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // Two tests share the name "logs in" → one finding on the 2nd; "signs up"
    // is unique → no finding.
    std::string f = dir + "/spec.js";
    write_file(f,
        "it('logs in', () => {})\n"
        "it('signs up', () => {})\n"
        "it('logs in', () => {})\n");   // duplicate of line 1
    auto res = keur::rules_scan_files(rules, {f});
    int hits = 0; int line = 0;
    for (auto& x : res) if (x.rule_id == "DUP-1") { hits++; line = x.line; }
    CHECK(hits == 1);     // fires once, on the duplicate
    CHECK(line == 3);     // at the SECOND occurrence
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

SCENARIO("[feature:rule-engine] multiline exclude (pattern-not) suppresses the safe case") {  // @proves engine-multiline
    // The multiline engine mirrors eng_pattern's exclude: if the window blob
    // also matches the pattern's exclude regex, the match is a negative case
    // (the mitigating call is present) and is skipped. This lets a multiline
    // rule flag "read user path then fs read" but NOT when a path.resolve
    // containment check is in the same window.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/MLX.rule",
        "id: MLX-1\ntitle: traversal without containment\ncategory: security\nseverity: error\n"
        "engine: multiline\nwindow: 5\n"
        "target:\n  extensions: .ts\n"
        "patterns:\n  - regex: req\\.query[\\s\\S]*?fs\\.readFile\n"
        "    exclude: path\\.resolve\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // vulnerable: user path flows to fs.readFile, no containment → one finding
    std::string vuln = dir + "/vuln.ts";
    write_file(vuln, "const p = req.query.file;\nconst t = base + p;\nfs.readFile(t);\n");
    auto fv = keur::rules_scan_files(rules, {vuln});
    int hv = 0; for (auto& f : fv) if (f.rule_id == "MLX-1") hv++;
    CHECK(hv == 1);   // the vulnerable shape fires

    // safe: a path.resolve containment check is in the window → excluded
    std::string safe = dir + "/safe.ts";
    write_file(safe, "const p = req.query.file;\nconst t = path.resolve(base, p);\nfs.readFile(t);\n");
    auto fs2 = keur::rules_scan_files(rules, {safe});
    int hsf = 0; for (auto& f : fs2) if (f.rule_id == "MLX-1") hsf++;
    CHECK(hsf == 0);  // the safe shape is excluded (pattern-not)
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] filename-case enforces a naming convention with an allowlist") {  // @proves engine-filename-case
    // The filename-case engine flags a file whose basename (scope=filename) does
    // not match the required case style, skipping names in `allow`. A sibling
    // rule with scope=dirname checks path directory segments the same way.
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/NC.rule",
        "id: NC-1\ntitle: md must be kebab\ncategory: quality\nseverity: warning\n"
        "engine: filename-case\nscope: filename\ncase: kebab\n"
        "allow: README TODO\n"
        "target:\n  extensions: .md\n");
    auto rules = keur::rules_load(dir);
    REQUIRE(!rules.empty());

    // kebab/lowercase names pass; an allowlisted ALL-CAPS name passes; a
    // SHOUTING or TitleCase name is flagged.
    write_file(dir + "/README.md", "x\n");         // allowed
    write_file(dir + "/my-doc.md", "x\n");          // kebab → ok
    write_file(dir + "/lucky13.md", "x\n");         // lowercase+digits → ok
    write_file(dir + "/TODO.md", "x\n");            // allowed
    write_file(dir + "/FOO.md", "x\n");             // ALL-CAPS → flagged
    write_file(dir + "/MyDoc.md", "x\n");           // Pascal → flagged (not kebab)
    auto f = keur::rules_scan_dir(rules, dir);
    std::set<std::string> flagged;
    for (auto& x : f) if (x.rule_id == "NC-1") flagged.insert(x.file);
    CHECK(flagged.count("FOO.md") == 1);
    CHECK(flagged.count("MyDoc.md") == 1);
    CHECK(flagged.count("README.md") == 0);   // allowlisted
    CHECK(flagged.count("TODO.md") == 0);      // allowlisted
    CHECK(flagged.count("my-doc.md") == 0);    // conforms
    CHECK(flagged.count("lucky13.md") == 0);   // conforms
    cleanup_dir(dir);
}


SCENARIO("[feature:rule-engine] file-absence over repo scope: present-but-unstaged does NOT fire") {  // @proves ADR-052.scope
    // A file-absence rule asks a REPO question ("has the project a SECURITY.md?"),
    // not a diff question. rules_scan_files_scoped evaluates file-level rules over
    // the whole repo tree, so a SECURITY.md that EXISTS in the repo but is not in
    // the staged set must NOT trigger the rule.
    // @arrange
    keur::test::TempDir t;
    REQUIRE(t.valid());
    write_file(t / "FA.rule",
        "id: FA-1\ntitle: repo has no SECURITY.md\ncategory: quality\nseverity: warning\n"
        "engine: file-absence\ntarget:\n  filenames: SECURITY.md\n");
    auto rules = keur::rules_load(t.path());
    REQUIRE(!rules.empty());
    // Repo contains SECURITY.md and a source file; only the source file is "staged".
    write_file(t / "SECURITY.md", "report here\n");
    write_file(t / "app.js", "console.log(1)\n");
    // @act
    auto f = keur::rules_scan_files_scoped(rules, {t / "app.js"}, t.path());
    // @assert
    CHECK(!has_rule(f, "FA-1"));   // SECURITY.md exists in the repo → no finding
    // @annihilate — automatic: t's dtor removes the tempdir on every exit path.
}

SCENARIO("[feature:rule-engine] file-absence over repo scope: truly absent still fires") {  // @proves ADR-052.scope
    // The complement: when the required file is absent from the WHOLE repo, the
    // rule must still fire — the fix must not silence the real case.
    // @arrange
    keur::test::TempDir t;
    REQUIRE(t.valid());
    write_file(t / "FA.rule",
        "id: FA-1\ntitle: repo has no SECURITY.md\ncategory: quality\nseverity: warning\n"
        "engine: file-absence\ntarget:\n  filenames: SECURITY.md\n");
    auto rules = keur::rules_load(t.path());
    REQUIRE(!rules.empty());
    write_file(t / "app.js", "console.log(1)\n");   // no SECURITY.md anywhere
    // @act
    auto f = keur::rules_scan_files_scoped(rules, {t / "app.js"}, t.path());
    // @assert
    CHECK(has_rule(f, "FA-1"));
    // @annihilate — automatic (TempDir dtor).
}

SCENARIO("[feature:rule-engine] KEUR-CFG-001 fires when a repo has no keur config") {  // @proves ADR-054.require
    auto rules = keur::rules_load(repo_root() + "/rules/keur-internal");
    REQUIRE(!rules.empty());
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/app.js", "console.log(1)\n");   // no keur config anywhere
    auto f = keur::rules_scan_files_scoped(rules, {dir + "/app.js"}, dir);
    CHECK(has_rule(f, "KEUR-CFG-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] KEUR-CFG-001 is satisfied by .config/keur.toml") {  // @proves ADR-054.dotconfig
    auto rules = keur::rules_load(repo_root() + "/rules/keur-internal");
    REQUIRE(!rules.empty());
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    write_file(dir + "/app.js", "console.log(1)\n");
    REQUIRE(::mkdir((dir + "/.config").c_str(), 0755) == 0);
    write_file(dir + "/.config/keur.toml", "[difficulty]\nintegrity = 5\n");
    auto f = keur::rules_scan_files_scoped(rules, {dir + "/app.js"}, dir);
    CHECK(!has_rule(f, "KEUR-CFG-001"));   // .config/keur.toml counts as the config
    CHECK(!has_rule(f, "KEUR-CFG-002"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] NODE-PIN-001 fires when package.json pins no Node version") {  // @proves NODE-PIN-001
    auto rules = keur::rules_load(repo_root() + "/rules/quality");
    REQUIRE(!rules.empty());
    std::string dir = create_temp_dir();
    REQUIRE(!dir.empty());
    // A JS repo (a .ts file makes facts resolve lang=ts) with a manifest that
    // declares a project but no engines.node.
    write_file(dir + "/app.ts", "export const x = 1;\n");
    write_file(dir + "/package.json", "{\n  \"name\": \"app\",\n  \"dependencies\": {}\n}\n");
    auto f = keur::rules_scan_dir(rules, dir);
    CHECK(has_rule(f, "NODE-PIN-001"));
    cleanup_dir(dir);
}

SCENARIO("[feature:rule-engine] NODE-PIN-001 is satisfied by engines.node (one-line and expanded)") {  // @proves NODE-PIN-001
    auto rules = keur::rules_load(repo_root() + "/rules/quality");
    REQUIRE(!rules.empty());
    // One-line engines block.
    std::string d1 = create_temp_dir();
    write_file(d1 + "/app.ts", "export const x = 1;\n");
    write_file(d1 + "/package.json", "{\"name\":\"app\",\"engines\":{\"node\":\">=20 <21\"}}\n");
    auto f1 = keur::rules_scan_dir(rules, d1);
    CHECK(!has_rule(f1, "NODE-PIN-001"));
    cleanup_dir(d1);
    // Expanded (multi-line) engines block — the node KEY is still on one line,
    // so keying on it is robust to formatting (no false positive here).
    std::string d2 = create_temp_dir();
    write_file(d2 + "/app.ts", "export const x = 1;\n");
    write_file(d2 + "/package.json",
        "{\n  \"name\": \"app\",\n  \"engines\": {\n    \"node\": \">=20.11 <21\"\n  }\n}\n");
    auto f2 = keur::rules_scan_dir(rules, d2);
    CHECK(!has_rule(f2, "NODE-PIN-001"));
    cleanup_dir(d2);
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
