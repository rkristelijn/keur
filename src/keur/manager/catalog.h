/**
 * @file manager/catalog.h
 * @brief Compile-time rule catalogue for the `keur manager` overlay.
 *
 * The overlay's source of truth: what THIS build of keur can enable/disable.
 * The list is generated from the .rule files at build time (see
 * scripts/gen-catalog.sh → manager/catalog_generated.cpp) and baked into the
 * binary, so the manager tree is never empty and moves with the version — a
 * newer keur ships a newer catalogue. Rule MATCHING still reads the .rule files
 * at runtime (rules stay data, ADR-009); this is only the index the TUI renders
 * and the set the disabled-lists in keur.toml switch on/off.
 *
 * Lifecycle: a rule marked `status: deprecated` in its .rule stays here with a
 * marker until the .rule is removed in a later version (then it drops out).
 *
 * @see docs/design/keur-manager-tree-tui.md
 * @id KEUR-MANAGER-CATALOG
 */
#pragma once
#include <string>
#include <vector>

namespace keur::manager {

/// One entry in the baked catalogue: identity + 4 grouping FACETS + a short
/// description + whether it is deprecated. The facets let the manager tree be
/// re-ordered (category-first, language-first, …); every rule has all four,
/// defaulting to "any" so the tree is complete and re-orderable with no gaps.
struct CatalogEntry {
    std::string id;
    std::string category;     ///< top bucket: security | secrets | supply-chain | …
    std::string subcategory;  ///< finer: sast | cmdi | sqli | iac | secrets | generic
    std::string language;     ///< derived from extensions: cpp | typescript | … | any
    std::string framework;    ///< angular | react | … | any (reserved; "any" for now)
    std::string description;  ///< the rule's title
    bool        deprecated = false;
};

/// The baked catalogue for this build (generated; sorted by category then id).
/// A reference to a function-local static — built once, no global init order.
[[nodiscard]] const std::vector<CatalogEntry>& catalog();

}  // namespace keur::manager
