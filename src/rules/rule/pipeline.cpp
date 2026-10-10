/**
 * @file rule/pipeline.cpp
 * @brief The literal-analyze pipeline (ADR-060): the closed verb set
 *        extract -> count -> classify, run over a fileset using a generic
 *        classify rule (ADR-059) that carries the class table.
 *
 * Read-only, aggregating, non-blocking (ADR-058 audit-first). The engine stays
 * simple: it sequences fixed verbs; the class table is data in the rule.
 */
#include "rule/internal.h"
#include "tokenizer.h"

#include <re2/re2.h>

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace keur {

namespace {

// --- verb: extract ---------------------------------------------------------
// Read each file, strip comments, emit string literals with first-seen file/line.
struct Occurrence { int count = 0; std::string file; int line = 0; };

void extract_into(const std::string& path,
                  std::unordered_map<std::string, Occurrence>& acc) {
  const LangSyntax* syn = lang_syntax(detail::get_ext(path));
  if (!syn) return;  // unknown language — no literals
  std::ifstream f(path);
  if (!f) return;
  std::stringstream ss;
  ss << f.rdbuf();
  for (const auto& lit : extract_literals(ss.str(), syn)) {
    auto& occ = acc[lit.text];
    if (occ.count == 0) { occ.file = path; occ.line = lit.line; }  // first seen
    occ.count++;
  }
}

// --- verb: classify --------------------------------------------------------
// First class whose regex matches wins (list order = priority). Returns the
// matched row index, or -1 if nothing matched.
int classify_one(const std::string& text,
                 const std::vector<std::unique_ptr<RE2>>& compiled) {
  for (size_t i = 0; i < compiled.size(); i++) {
    if (compiled[i] && RE2::PartialMatch(text, *compiled[i])) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

std::vector<ClassifiedLiteral> run_literal_pipeline(const Rule& classify_rule,
                                                    const std::vector<std::string>& files) {
  // verb: extract + verb: count (fused — count as we extract).
  std::unordered_map<std::string, Occurrence> counts;
  for (const auto& path : files) extract_into(path, counts);

  // pre-compile the class table regexes once (list order preserved).
  std::vector<std::unique_ptr<RE2>> compiled;
  compiled.reserve(classify_rule.classes.size());
  RE2::Options opt;
  opt.set_log_errors(false);
  for (const auto& c : classify_rule.classes)
    compiled.push_back(std::make_unique<RE2>(c.regex_str, opt));

  // verb: classify — one row per unique literal.
  std::vector<ClassifiedLiteral> out;
  out.reserve(counts.size());
  for (const auto& [text, occ] : counts) {
    ClassifiedLiteral cl;
    cl.text = text;
    cl.count = occ.count;
    cl.file = occ.file;
    cl.line = occ.line;
    int idx = classify_one(text, compiled);
    if (idx >= 0) {
      const LiteralClass& lc = classify_rule.classes[idx];
      cl.klass = lc.name;
      cl.bucket = lc.bucket;
      cl.advice = lc.advice;
      // magic bucket honours the rule-of-three threshold: below it = not flagged
      // (treat as noise so the report does not nag on a value seen once/twice).
      if (lc.bucket == "magic" && lc.threshold > 0 && occ.count < lc.threshold)
        cl.bucket = "noise";
    }
    out.push_back(std::move(cl));
  }
  return out;
}

}  // namespace keur
