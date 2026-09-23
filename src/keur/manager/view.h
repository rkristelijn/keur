/**
 * @file manager/view.h
 * @brief Pure rendering of the manager tree to terminal lines.
 *
 * `render(model, opts) -> vector<string>` is a pure function: given the model
 * (tree + cursor) and view options (height, width, colour), it produces the
 * lines to print. No I/O, no terminal calls — so the layout is snapshot-tested
 * by asserting on the returned strings. The interactive loop owns the actual
 * screen writes.
 *
 * @see docs/design/keur-manager-tree-tui.md
 * @implements ADR-003
 * @id KEUR-MANAGER-VIEW
 */
#pragma once
#include <string>
#include <vector>

#include "manager/model.h"

namespace keur::manager {

/// View options — the terminal geometry + whether to emit colour.
struct ViewOpts {
    int         rows = 24;    ///< terminal height in lines
    int         cols = 80;    ///< terminal width in columns
    bool        color = true; ///< emit SGR colour escapes (false honours NO_COLOR/pipe)
    std::string version;      ///< baked KEUR_VERSION, shown in the header ("" = omit)
};

/// Render the whole frame: a header, the scrolled tree with the cursor marked,
/// and a footer keybinding hint. The tree scrolls so the cursor stays visible
/// within `opts.rows`. Pure.
[[nodiscard]] std::vector<std::string> render(const Model& m, const ViewOpts& opts);

/// Render one tree row's text (glyphs + label + state), WITHOUT colour or the
/// cursor marker — the stable core the full renderer decorates. Exposed for
/// focused unit testing. Pure.
[[nodiscard]] std::string render_row(const Row& row);

}  // namespace keur::manager
