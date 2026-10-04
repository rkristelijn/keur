/**
 * @file cmd/settings.h
 * @brief keur settings — the MEDIOR difficulty view: per-ility sliders grouped
 *        by ISO 25010 characteristic. The middle lens between `keur level`
 *        (coarse, DOOM) and `keur manager` (expert, per-rule), over the ONE
 *        canonical state (keur.toml [difficulty] vector). See the design doc.
 *
 * The model + reducers + renderer here are PURE (no tty, no file I/O) so the
 * whole interaction is unit-testable by feeding key events and asserting the
 * resulting state — the "pure model + reducers" the manager already follows.
 * The interactive loop (settings()) owns the I/O edge: raw mode, key input,
 * and the TOML write (via the Fs adapter), mirroring cmd/level.cpp.
 *
 * @see docs/design/difficulty-chooser-ui.md
 * @implements ADR-038
 * @implements ADR-039
 * @id KEUR-SETTINGS
 */
#pragma once
#include <string>
#include <vector>

#include "rules/rule_engine.h"

namespace keur::cmd {

// ---------------------------------------------------------------------------
// The ISO 25010 characteristic -> ility map (the tree's static shape, data).
// Mirrors ADR-039 §1 / rule/resolve.cpp's category->ility fallback groups, so
// the sliders cover exactly the ilities the corpus can declare.
// @id KEUR-SETTINGS.iso25010
// ---------------------------------------------------------------------------

/// One ISO 25010 characteristic (a group of ilities), e.g. "Security".
struct IsoGroup {
    std::string                 name;      ///< display name ("Security")
    std::vector<std::string>    ilities;   ///< ility keys under it, in order
};

/// The fixed group -> ility map (ISO 25010 sub-characteristics + the ADR-010
/// process ility `traceability`). Pure data, order-stable for deterministic
/// rendering. @id KEUR-SETTINGS.iso25010
[[nodiscard]] const std::vector<IsoGroup>& iso25010_groups();

// ---------------------------------------------------------------------------
// Pure model: a two-level tree (group -> ility slider) over the resolved
// DifficultyVector. Each ility carries a level 0..5 (0 == OFF). A group is
// expandable; collapsed it shows the average of its non-OFF ilities.
// @id KEUR-SETTINGS.model
// ---------------------------------------------------------------------------

/// A slider row: one ility with its current level (0 == OFF) and a per-rule
/// override marker (`*`) when a rule under this ility is force on/off.
struct SliderNode {
    std::string ility;            ///< ility key ("confidentiality")
    int         level = 0;        ///< 0 == OFF, else 1..5
    bool        has_override = false;  ///< a per-rule override touches this ility
};

/// A group row: an ISO 25010 characteristic with its sliders + collapse state.
struct GroupNode {
    std::string              name;             ///< "Security"
    std::vector<SliderNode>  sliders;
    bool                     expanded = false; ///< collapsed by default (overall)
};

/// The whole settings tree + cursor + dirty flag. Pure data.
struct SettingsModel {
    std::vector<GroupNode> groups;
    int  cursor = 0;        ///< index into visible_rows()
    bool dirty  = false;    ///< unsaved slider edits since last save
};

/// A flattened visible row: either a group header or a slider under an expanded
/// group. `group`/`slider` index back into the model (slider == -1 on a group).
struct SettingsRow {
    int group  = 0;    ///< index into model.groups
    int slider = -1;   ///< -1 == the group header; else index into group.sliders
    bool is_group() const { return slider < 0; }
};

// --- Build ------------------------------------------------------------------

/// Build the model from a resolved DifficultyVector + the set of ility keys
/// that carry a per-rule override (for the `*` marker). The tree shape is the
/// fixed ISO 25010 map; each slider's level is read from the vector (level_of).
/// All groups start collapsed (the "overall" view). Pure.
/// @id KEUR-SETTINGS.build
[[nodiscard]] SettingsModel build_settings_model(
    const DifficultyVector& vec,
    const std::vector<std::string>& overridden_ilities = {});

// --- Queries (pure) ---------------------------------------------------------

/// The currently-visible rows top-to-bottom (sliders of collapsed groups
/// excluded). Deterministic order: group, then its sliders when expanded.
[[nodiscard]] std::vector<SettingsRow> settings_visible_rows(const SettingsModel& m);

/// The average level of a group's non-OFF ilities, rounded to one decimal. When
/// every ility is OFF, returns 0.0 (shown as "off"). Pure.
/// @id KEUR-SETTINGS.average
[[nodiscard]] double group_average(const GroupNode& g);

/// True when the group's non-OFF ilities are not all equal, OR any slider under
/// it has a per-rule override — the `*` marker condition. Pure.
/// @id KEUR-SETTINGS.star
[[nodiscard]] bool group_has_star(const GroupNode& g);

/// True when ANY group would show a `*` (the overall/global marker). Pure.
[[nodiscard]] bool model_has_star(const SettingsModel& m);

// --- Reducers (pure: mutate the model, no I/O) ------------------------------

/// Move the cursor by `delta` visible rows, clamped. @id KEUR-SETTINGS.move
void settings_move(SettingsModel& m, int delta);

/// Expand the focused group (no-op on a slider or already-expanded group).
/// Returns true if it changed. @id KEUR-SETTINGS.expand
bool settings_expand(SettingsModel& m);

/// Collapse the focused group; on a slider row, collapse its owning group and
/// move the cursor to that group header (tree convention). Returns true if it
/// changed. @id KEUR-SETTINGS.collapse
bool settings_collapse(SettingsModel& m);

/// Adjust the focused slider by `delta` steps along off(0)..5, clamped. On a
/// group header, set ALL its sliders to the same new level (drag the group) —
/// the average moves as one. Sets `dirty`. Returns true if anything changed.
/// @id KEUR-SETTINGS.adjust
bool settings_adjust(SettingsModel& m, int delta);

/// Collect the current vector state (ility -> level) from the model, for the
/// writer. Only emits ilities the model knows (the ISO map). Pure.
/// @id KEUR-SETTINGS.collect
[[nodiscard]] std::vector<std::pair<std::string, int>> settings_vector(const SettingsModel& m);

// ---------------------------------------------------------------------------
// Pure renderer: (model, opts) -> a screen string. Sliders as bars, OFF a
// detached leftmost stop, `*` on deviating groups/ilities.
// @id KEUR-SETTINGS.render
// ---------------------------------------------------------------------------

/// Render the settings screen to a string. `color` toggles ANSI; the cursor row
/// is marked. `show_hint` appends the standalone key legend (←/→ adjust); the
/// router passes false and prints its own (←/→ switch lens) hint. Pure.
/// @id KEUR-SETTINGS.render
[[nodiscard]] std::string render_settings_screen(const SettingsModel& m, bool color,
                                                 bool show_hint = true);

// --- Persist (pure TOML round-trip; file I/O done by the caller) ------------

/// Upsert the `[difficulty]` section of a keur.toml body with the given ility
/// levels (level 0 is written as `"off"`, 1..5 as the number), preserving every
/// other line. Adds the section when absent, replaces existing keys in place.
/// Mirrors upsert_policy_level. Pure (string in -> string out).
/// @id KEUR-SETTINGS.persist
[[nodiscard]] std::string upsert_difficulty(
    const std::string& toml_body,
    const std::vector<std::pair<std::string, int>>& ilities);

// --- Options + entrypoint ---------------------------------------------------

/// Parsed `keur settings` invocation. Mirrors LevelOptions.
struct SettingsOptions {
    std::string config;             ///< --config path (empty = auto-resolve)
    bool        no_color   = false; ///< --no-color
    bool        global     = false; ///< --global: write the machine-wide config
    bool        print_only = false; ///< --print: non-interactive render, no tty
};

/// Parse argv into SettingsOptions. Pure, never fails (unknown flags ignored).
/// @id KEUR-SETTINGS.parse
[[nodiscard]] SettingsOptions settings_parse(const std::vector<std::string>& args);

/// Resolve the on-disk difficulty vector (default→global→repo cascade) + the
/// ilities carrying a per-rule override, and build the slider model from it.
/// This is the ONE load path; both `keur settings` and the `tune` router use
/// it so the medior view can never drift from the resolved state. I/O.
/// @id KEUR-SETTINGS.load
[[nodiscard]] SettingsModel load_settings_model();

/// Persist a slider model's vector to the target keur.toml (repo, or global
/// when `global`), keeping one `.bak` slot. The ONE save path, shared with the
/// router. Returns true on success and the written path in `out_path`. I/O.
/// @id KEUR-SETTINGS.save
bool save_settings_model(const SettingsOptions& opts, const SettingsModel& model,
                         std::string* out_path = nullptr);

/// Run `keur settings`. Interactive by default (arrows move/expand/collapse,
/// -/+ or left/right adjust, `s` saves to keur.toml); --print renders once and
/// returns. Returns 0 on success. @id KEUR-SETTINGS
int settings(const SettingsOptions& opts);

}  // namespace keur::cmd
