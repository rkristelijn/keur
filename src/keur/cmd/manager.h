/**
 * @file cmd/manager.h
 * @brief keur manager — one read-only overview of everything that governs a
 *        commit: config scopes, hooks (+ coexisting managers), scanners, rules.
 *
 * `keur manager --status [--json]` aggregates the data layers that are today
 * spread across `keur config list` and `keur hook --status` into a single
 * board — the data model behind the future tree TUI. Read-only in this layer;
 * toggling/editing arrive later (see docs/design/keur-manager-tree-tui.md).
 *
 * All state comes through injected adapters (Git, Fs) + the pure config
 * modules (scopes, hookmgr, scanners), so the whole thing is spoofable and
 * unit-testable with no real repo, files, or tools. @implements ADR-003
 */
#pragma once
#include <string>
#include <vector>

#include "adapters/fs.h"
#include "adapters/git.h"
#include "config/hookmgr.h"
#include "config/scanners.h"
#include "config/scopes.h"

namespace keur::cmd {

/// Parsed `keur manager` options.
struct ManagerOptions {
    std::string subcommand = "status";  ///< only "status" for now
    std::string config;                 ///< --config explicit path ("" = none)
    bool        json = false;           ///< --json machine-readable output
    bool        interactive = false;    ///< bare `keur manager` → tree TUI
    bool        no_color = false;       ///< --no-color / NO_COLOR: plain output
};

/// Injectable adapters for the manager command (defaults = real).
struct ManagerDeps {
    adapters::Git git{};
    adapters::Fs  fs{};
};

/// One rule category with how many rules it holds.
struct RuleCategoryCount {
    std::string category;
    int         count = 0;

    RuleCategoryCount() = default;
    // Explicit constructor so vector::emplace_back works on every toolchain:
    // libc++ (macOS Xcode CI) cannot emplace a constructor-less aggregate.
    // @see KEUR-PORT-007
    RuleCategoryCount(std::string category_, int count_)
        : category(std::move(category_)), count(count_) {}
};

/// The aggregated board — the tree's data model, resolved once.
struct ManagerBoard {
    std::vector<config::Layer>       config_layers;
    config::HookSituation            hooks;
    std::vector<config::Scanner>     scanners;
    std::vector<RuleCategoryCount>   rule_categories;
    int                              rule_total = 0;
};

/// Parse `status` / `--json` / `--config <path>` from argv.
ManagerOptions manager_parse(const std::vector<std::string>& args);

/// Run the manager subcommand. Returns 0 on success, 1 on error.
int manager(const ManagerOptions& opts, const ManagerDeps& deps = ManagerDeps{});

/// Run the interactive tree TUI (bare `keur manager`). Enters raw mode, renders
/// the Scope→Category→Rule tree, and applies pure reducers to key events;
/// `s` saves pending toggles through the shared rules persister, `q` prompts
/// when there are unsaved changes. Returns 0 on clean exit.
int manager_interactive(const ManagerOptions& opts, const ManagerDeps& deps = ManagerDeps{});

/// Render the board as human-readable text lines. Pure.
[[nodiscard]] std::vector<std::string> manager_render_text(const ManagerBoard& board);

/// Render the board as a single JSON object string. Pure.
[[nodiscard]] std::string manager_render_json(const ManagerBoard& board);

}  // namespace keur::cmd
