/**
 * @file cmd/level.h
 * @brief keur level — the DOOM "Choose your Difficulty" screen. Picks a KEUR
 *        maturity level (CMMI L1..L5) and persists it to keur.toml so the gate's
 *        strictness follows it (the ADR-026 tie-in). The logo is responsive
 *        coloured braille (render/logo.*); the level list renders as coloured
 *        TEXT. I/O (terminal size, key input, file write) stays at the edge —
 *        adapters/ + common/platform (ADR-003). @see docs/design/doom-level-select.md
 * @id KEUR-LEVEL
 */
#pragma once
#include <string>
#include <vector>

#include "render/logo.h"

namespace keur::cmd {

/// One selectable difficulty: a DOOM-flavoured name mapped to a CMMI maturity
/// level with a short description block. Pure data — no I/O.
struct LevelInfo {
    int         cmmi;         ///< 1..5 (CMMI maturity, ADR-026)
    const char* name;         ///< DOOM-style label ("KEUR Me Plenty")
    const char* cmmi_name;    ///< CMMI level name ("Defined")
    const char* description;  ///< one-line blurb shown under the selection
};

/// The five fixed levels, index 0..4 == CMMI 1..5. Table-testable.
/// @id KEUR-LEVEL.table
[[nodiscard]] const std::vector<LevelInfo>& level_table();

/// Number of difficulty levels (CMMI 1..5). A compile-time constant so callers
/// clamp/iterate without truncating a size_t into int (ADR-006).
constexpr int k_level_count = 5;

/// Parsed `keur level` invocation.
struct LevelOptions {
    std::string config;              ///< --config path (empty = auto-resolve)
    bool        no_color   = false;  ///< --no-color: never emit ANSI
    bool        print_only = false;  ///< non-interactive: --print / --bucket set
    int         print_level = 0;     ///< --print <N> (1..5), 0 = unset
    bool        bucket_set  = false; ///< --bucket <sm|md|lg|xl> was given
    render::Bucket bucket = render::Bucket::Md;  ///< forced bucket (--bucket)
    bool        global      = false; ///< --global: write to the GLOBAL keur.toml
                                     ///< (~/.config/keur/keur.toml) instead of
                                     ///< the repo-local one — a machine-wide
                                     ///< default difficulty for all repos.
};

/// Parse argv into LevelOptions. Pure — no I/O. Unknown flags are ignored so
/// the parser never fails the command; validation happens in level().
/// @id KEUR-LEVEL.parse
[[nodiscard]] LevelOptions level_parse(const std::vector<std::string>& args);

/// Map a bucket name ("sm"/"md"/"lg"/"xl") to a Bucket. Returns false for an
/// unknown name (out stays untouched). Pure.
/// @id KEUR-LEVEL.bucket_name
[[nodiscard]] bool bucket_from_name(const std::string& name, render::Bucket& out);

/// Render the level-select screen (logo + title + 5 levels + description) to a
/// string, with `selected` (1..5) marked. Pure: takes the bucket, colour flag
/// and selection explicitly, does no I/O. This is what --print emits and what
/// the interactive loop draws each frame. `show_logo` false omits the braille
/// logo (used on short terminals — see fits_logo).
/// @id KEUR-LEVEL.render
[[nodiscard]] std::string render_level_screen(render::Bucket bucket, bool color,
                                              int selected, bool show_logo = true);

/// Whether the logo should be shown at all for a terminal of `rows` height.
/// Below the space the smallest logo + the chooser UI needs, we omit the logo
/// entirely rather than clip it (a clipped "KEUR" reads as "neuk"). Pure.
/// @id KEUR-LEVEL.fits-logo
[[nodiscard]] bool fits_logo(int rows);

/// Upsert `level = N` under a `[policy]` section in a keur.toml body, preserving
/// every other line. Adds the section/key when absent, replaces the value when
/// present. Pure (string in → string out); the file I/O is done by the caller
/// through the Fs adapter (ADR-003). @id KEUR-LEVEL.persist
[[nodiscard]] std::string upsert_policy_level(const std::string& toml_body, int level);

/// Run the `keur level` subcommand. Interactive by default (arrow keys +
/// Enter, persists the choice to keur.toml); non-interactive with --print /
/// --bucket (renders deterministically, no keyboard). Returns 0 on success.
int level(const LevelOptions& opts);

}  // namespace keur::cmd
