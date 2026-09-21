/**
 * @file cmd/config.h
 * @brief keur config — inspect the resolved config layer stack.
 *
 * `keur config list [--json]` prints every config layer (defaults → global →
 * keur-home → repo → explicit), its path, and whether it exists on disk, so a
 * human (or an AI reading --json) can see exactly where config comes from and
 * where a change would land. Read-only in this layer; get/set/history arrive in
 * later layers (see docs/design/keur-manager-tree-tui.md).
 *
 * I/O is injected via adapters (Git for the repo root, Fs for existence), so
 * the command is fully spoofable in tests. @implements ADR-003
 */
#pragma once
#include <string>
#include <vector>

#include "adapters/fs.h"
#include "adapters/git.h"
#include "config/scopes.h"

namespace keur::cmd {

/// Parsed `keur config` options.
struct ConfigOptions {
    std::string subcommand = "list";  ///< only "list" for now
    std::string config;               ///< --config explicit path ("" = none)
    bool        json = false;         ///< --json machine-readable output
};

/// Injectable adapters for the config command (defaults = real).
struct ConfigDeps {
    adapters::Git git{};
    adapters::Fs  fs{};
};

/// Parse `list` / `--json` / `--config <path>` from argv.
ConfigOptions config_parse(const std::vector<std::string>& args);

/// Run the config subcommand. Returns 0 on success, 1 on error.
int config(const ConfigOptions& opts, const ConfigDeps& deps = ConfigDeps{});

/**
 * Render one config layer as a display line (padded scope name + path +
 * present/absent). Pure. Shared by `config list` and `manager --status` so both
 * render a layer identically — one renderer, no drift.
 */
std::string config_layer_line(const config::Layer& layer);

/**
 * Render the layer stack as text lines (one per layer). Pure: takes the already
 * resolved layers, returns display strings. @proves via unit test.
 */
std::vector<std::string> config_render_text(const std::vector<config::Layer>& layers);

/// Render the layer stack as a single JSON array string. Pure.
std::string config_render_json(const std::vector<config::Layer>& layers);

}  // namespace keur::cmd
