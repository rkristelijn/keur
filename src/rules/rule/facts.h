/**
 * @file rule/facts.h
 * @brief Facts pass (ADR-009) — a cheap, first-class context map for rules.
 * @implements ADR-009.facts
 *
 * Before matching, one light pass establishes per-target FACTS: a small, flat,
 * language-neutral map that rules join on via `requires:`. Facts are derived
 * from DIRECT manifest pins and file presence ONLY — no dependency graph, no
 * network, no lockfile walk (that is SCA, explicitly out of scope in ADR-009).
 *
 * The engine gains a derivation pass, not a brain: which frameworks/versions
 * matter is data in the rules, not knowledge here. This file only knows how to
 * read a handful of well-known manifest shapes into a flat map.
 *
 * Adapter seam (ADR-003): each reader is a PURE function of file CONTENT
 * (string in, facts out), so it is unit-testable without a filesystem. The
 * directory walk that feeds them content is the only I/O, kept in one place.
 */
#ifndef KEUR_RULE_FACTS_H
#define KEUR_RULE_FACTS_H

#include <map>
#include <set>
#include <string>

namespace keur {

/** The flat facts map rules join on. Language-neutral, shallow by design. */
struct Facts {
  std::set<std::string> langs;                    // from file extensions present
  std::set<std::string> frameworks;               // from manifest deps + markers
  std::map<std::string, std::string> versions;    // DIRECT manifest pins only
  std::map<std::string, std::string> runtimes;    // engines / base-image tags
};

// --- pure readers (adapter seam): content in, facts merged in place ----------
// Each reads ONE manifest shape. Unknown/garbage content is a no-op (never throws).

/** package.json: dependencies/devDependencies -> frameworks+versions; engines -> runtimes. */
void facts_read_package_json(const std::string& content, Facts& out);

/** go.mod: `require` pins -> frameworks+versions; `go X.Y` -> runtimes[go]. */
void facts_read_go_mod(const std::string& content, Facts& out);

/** Dockerfile: `FROM image:tag` -> runtimes (base image) + framework marker. */
void facts_read_dockerfile(const std::string& content, Facts& out);

/** pom.xml: <dependency> groupId/artifactId -> frameworks; <version>/<java.version> -> versions/runtimes. */
void facts_read_pom_xml(const std::string& content, Facts& out);

/** composer.json (PHP): require map -> frameworks+versions; require.php -> runtimes[php]. */
void facts_read_composer_json(const std::string& content, Facts& out);

/** Map a file extension (with dot, e.g. ".ts") to a language name, or "" if unknown. */
std::string facts_lang_for_ext(const std::string& ext);

/** Derive the full facts map for a project directory (the only I/O entry point). */
Facts derive_facts(const std::string& root);

/** Render facts as one JSON object (for `keur scan --facts` / debugging). */
std::string facts_to_json(const Facts& f);

}  // namespace keur

#endif  // KEUR_RULE_FACTS_H
