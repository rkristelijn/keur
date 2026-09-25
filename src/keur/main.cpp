/**
 * @file main.cpp
 * @brief keur — deterministic guardrails for AI-generated code.
 *
 * Single-binary entry point.  Uses a command-dispatch table (unordered_map of
 * lambdas) instead of an if/else chain.  Adding a subcommand is one line.
 *
 * Usage:
 *   keur hook   [--install | --global | --status | --remove [--global]]
 *   keur scan   [path]
 *   keur check  [--tier pre-commit|pre-push] [--ai off]
 *   keur policy explain
 *   keur version
 *   keur help
 */
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cmd/check.h"
#include "cmd/config.h"
#include "cmd/hook.h"
#include "cmd/learn.h"
#include "cmd/level.h"
#include "cmd/manager.h"
#include "cmd/policy.h"
#include "cmd/rules.h"
#include "cmd/scan.h"

// ---------------------------------------------------------------------------
// Type alias for a subcommand handler
// ---------------------------------------------------------------------------
using Args    = std::vector<std::string>;
using Handler = std::function<int(const Args&)>;

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------
static void usage() {
    std::cout <<
        "keur — deterministic guardrails for AI-generated code\n"
        "\n"
        "Commands:\n"
        "  hook  --install            Install per-repo hooks (.git/hooks)\n"
        "  hook  --global             Install global hooks (~/.config/git/hooks)\n"
        "  hook  --augment [--remove] Chain keur into the repo's existing hook\n"
        "                             manager (e.g. Husky) so it runs even when a\n"
        "                             manager owns core.hooksPath. Guarded: runs\n"
        "                             only if keur is installed. Commit to share.\n"
        "  hook  --status             Show which hooks are active\n"
        "  hook  --remove [--global]  Uninstall hooks\n"
        "  scan  [path]               Run the scanner pipeline over a directory\n"
        "        [--format F]         F = jsonl (default) | compact | verbose | sarif\n"
        "        [--color C]          C = auto (default) | always | never\n"
        "  check [--tier T]           Gate the staged diff (used by hooks)\n"
        "                             T = pre-commit (default) | pre-push\n"
        "  policy explain             Print the resolved policy and thresholds\n"
        "  rules list [--winners]     Show the resolved rule cascade (layers + specificity)\n"
        "  rules disable <id>         Turn a rule off (writes [rules] disabled to keur.toml)\n"
        "  rules enable  <id>         Turn a rule back on   [--scope repo|global]\n"
        "  config list [--json]       Show the resolved config layer stack (read-only)\n"
        "  manager --status [--json]  One overview: config, hooks, scanners, rules\n"
        "  level [--print N]          DOOM-style difficulty picker (CMMI L1..L5).\n"
        "        [--bucket sm|md|lg|xl] Interactive by default; --print/--bucket\n"
        "        [--no-color] [--global]  render deterministically. Persists to\n"
        "        keur.toml (--global: the machine-wide ~/.config/keur/keur.toml).\n"
        "  learn [--stop|--status]    Spin the optimise loop + live TUI monitor\n"
        "        [--source S]         S = corpus (default) | keureval | alternate\n"
        "        [--model M]          Ollama model for the loop (default: keur-rule-engineer)\n"
        "  version                    Print version\n"
        "  help                       Show this message\n"
        "\n"
        "Environment:\n"
        "  KEUR_HOME        root directory of a keur installation\n"
        "  KEUR_RULES_DIRS  colon-separated extra rule directories\n"
        "\n"
        "Design: findings are evidence; the policy engine decides PASS/WARN/BLOCK.\n";
}

// ---------------------------------------------------------------------------
// Command dispatch table
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, Handler> k_commands = {
    {"hook",   [](const Args& a) { return keur::cmd::hook(keur::cmd::hook_parse(a)); }},
    {"scan",   [](const Args& a) { return keur::cmd::scan(keur::cmd::scan_parse(a)); }},
    {"check",  [](const Args& a) { return keur::cmd::check(keur::cmd::check_parse(a)); }},
    {"policy", [](const Args& a) { return keur::cmd::policy(keur::cmd::policy_parse(a)); }},
    {"rules",  [](const Args& a) { return keur::cmd::rules(keur::cmd::rules_parse(a)); }},
    {"config", [](const Args& a) { return keur::cmd::config(keur::cmd::config_parse(a)); }},
    {"manager", [](const Args& a) { return keur::cmd::manager(keur::cmd::manager_parse(a)); }},
    {"level",  [](const Args& a) { return keur::cmd::level(keur::cmd::level_parse(a)); }},
    {"learn",  [](const Args& a) { return keur::cmd::learn(keur::cmd::learn_parse(a)); }},
    {"version",  [](const Args&) {
        // Prefer the version from a resolved keur.toml; fall back to the
        // compile-time KEUR_VERSION baked in by the Makefile.
        auto cfg  = keur::cmd::resolve_config("");
        auto poly = keur::cmd::policy_load(cfg);
        if (!poly.version.empty()) std::cout << poly.version << "\n";
#ifdef KEUR_VERSION
        else std::cout << KEUR_VERSION << "\n";
#else
        else std::cout << "unknown\n";
#endif
        return 0;
    }},
    {"--version", [](const Args& a) { return k_commands.at("version")(a); }},
    {"-v",        [](const Args& a) { return k_commands.at("version")(a); }},
    {"help",    [](const Args&) { usage(); return 0; }},
    {"--help",  [](const Args&) { usage(); return 0; }},
    {"-h",      [](const Args&) { usage(); return 0; }},
};

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 0; }

    std::string cmd = argv[1];
    Args args;
    args.reserve(static_cast<std::size_t>(argc - 2));
    for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);

    auto it = k_commands.find(cmd);
    if (it != k_commands.end()) return it->second(args);

    std::cerr << "keur: unknown command '" << cmd << "'\n";
    usage();
    return 1;
}
