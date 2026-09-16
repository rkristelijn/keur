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
#include "cmd/hook.h"
#include "cmd/policy.h"
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
        "  hook  --status             Show which hooks are active\n"
        "  hook  --remove [--global]  Uninstall hooks\n"
        "  scan  [path]               Run the scanner pipeline over a directory\n"
        "  check [--tier T]           Gate the staged diff (used by hooks)\n"
        "                             T = pre-commit (default) | pre-push\n"
        "  policy explain             Print the resolved policy and thresholds\n"
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
    {"hook",   [](const Args& a) { return keur::cmd_hook(keur::cmd_hook_parse(a)); }},
    {"scan",   [](const Args& a) { return keur::cmd_scan(keur::cmd_scan_parse(a)); }},
    {"check",  [](const Args& a) { return keur::cmd_check(keur::cmd_check_parse(a)); }},
    {"policy", [](const Args& a) { return keur::cmd_policy(keur::cmd_policy_parse(a)); }},
    {"version",  [](const Args&) {
        // Prefer the version from a resolved keur.toml; fall back to the
        // compile-time KEUR_VERSION baked in by the Makefile.
        auto cfg  = keur::resolve_config("");
        auto poly = keur::policy_load(cfg);
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
