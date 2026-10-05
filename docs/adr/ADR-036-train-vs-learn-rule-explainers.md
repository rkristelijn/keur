# ADR-036: `keur train` (the optimise loop) and `keur learn <RULE>` (explain a rule)

@id ADR-036

## Status

Proposed · 2026-09-27 · Coverage: none
· Relates to: ADR-032 (trustworthy ground truth), ADR-035 (self-explaining findings),
`docs/design/learning-loop.md`, `src/keur/cmd/learn.cpp`

## Context

Today `keur learn` spins the self-improving optimise loop (Ollama proposes rules,
the benchmark gates them — `docs/design/learning-loop.md`). The verb is wrong for
two reasons:

1. **It is keur that is being trained, not the user.** The loop *trains keur's
   own rule set* — grows `rules/learned/` against ground truth. From the user's
   point of view "learn" reads as "help ME learn", which is the opposite of what
   the command does.
2. **There is no command for the thing a user actually wants to learn.** When a
   BLOCK fires (`KEUR-CMDI-003 | ...`), a developer or an AI often wants MORE than
   the one-line message + fix hint (ADR-035): the *why*, the CWE background, safe
   vs unsafe examples, references. That explainer does not exist as a command.

So the natural, honest split is: **`keur train` = train the detector** (the loop),
**`keur learn <RULE>` = the user/AI learns about a rule** (fetch its explainer).

This also fits keur's mission (`vision.md`): a copilot for coding and
vibe-coding, where "clear errors so a human OR an AI knows exactly what to fix"
is the point. An on-demand, per-rule explainer is that promise deepened — the AI
that hit a BLOCK can pull the full rationale and self-correct.

## Decision

### 1. Rename: `keur learn` → `keur train`

The optimise-loop command becomes `keur train` (same flags: `--stop`, `--status`,
`--source`, `--model`, …). `learn` is freed for the new meaning below.

- Keep `keur learn` as a **deprecated alias** for `keur train` for one release
  (prints a deprecation note pointing at `keur train`), then repurpose it fully.
  This avoids breaking the running `supervise.sh`/`keur learn` muscle memory and
  any scripts mid-flight.

### 2. New: `keur learn <RULE-ID>` — fetch a rule's explainer markdown

`keur learn KEUR-CMDI-003` fetches and prints the explainer markdown for that
rule. The explainer lives in the keur repo under a predictable path:

```text
docs/rules/<RULE-ID>.md        e.g. docs/rules/KEUR-CMDI-003.md
```

Resolution order (offline-first, IP never forced online):

1. **Local**: `$KEUR_HOME/docs/rules/<RULE-ID>.md` (shipped with the binary/repo).
2. **Cache**: `$XDG_CACHE_HOME/keur/rules/<RULE-ID>.md` (previously fetched).
3. **Remote (opt-in)**: fetch from the keur repo raw URL
   (`https://raw.githubusercontent.com/<org>/keur/main/docs/rules/<RULE-ID>.md`)
   only when `--fetch` is passed or `KEUR_LEARN_FETCH=1` is set, then cache it.
   Default is offline — consistent with "your IP never leaves your machine"
   (`vision.md`): a *download of keur's own docs* is fine, but must be explicit,
   never automatic, and it never uploads anything.

If no explainer exists, fall back to what keur already knows: print the rule's
`title`, `message`, `fix`, category, CWE (from the `.rule` file) — so `keur learn`
is always useful, and a missing markdown degrades gracefully.

### 3. The explainer markdown — content contract

Each `docs/rules/<RULE-ID>.md` is a small, uniform doc (a template ships in
`docs/rules/_TEMPLATE.md`):

```markdown
# KEUR-CMDI-003 — OS command injection via os.popen family
category: cmdi · severity: critical · CWE-78

## Why this matters
<the risk in 2–3 sentences>

## Unsafe (what fires)
```py
os.popen(cmd)            # variable arg → injection
```

## Safe (what does not)

```py
subprocess.run(["ls","-la"])   # argv vector, no shell
```

## Fix

<the remediation — same as the rule's fix: field, expanded>

## Suppress (if genuinely a false positive)

`# keur:ignore KEUR-CMDI-003` (or a `.keurignore` entry — ADR-035)

## References

- CWE-78, OWASP …

```text

The BLOCK output (ADR-035) gains one line pointing here:
`learn more: keur learn KEUR-CMDI-003`.

### 4. `keur train` GENERATES the explainer — docs are a by-product, not a chore

The obvious cost of §2/§3 is "now every rule needs a hand-written markdown." The
resolution: **the train loop already runs an LLM (Ollama) that invents the rule
from a real failing case — it writes the explainer in the same step**, with the
context it already has (the ground-truth case, the regex, the category/CWE, the
`fix:`). A rule and its explainer are born together; documentation becomes a
by-product of training, not a separate backlog.

When the acceptance gate ACCEPTS a candidate rule (recall up, precision held,
generalises — ADR-032), the loop also:

1. asks the model, from the SAME context, for the explainer sections
   (why / unsafe / safe / fix / references) — one extra prompt per *accepted*
   rule (not per attempt), so negligible loop cost;
2. writes `docs/rules/<RULE-ID>.md` beside the accepted `rules/learned/<id>.rule`;
3. logs it in `eval/history/`, so an explainer is as traceable as the rule.

Guardrails (same discipline as the rule gate):
- The explainer is **generated then human/PR-reviewable** — it is docs, not a
  gate, so an LLM draft is an acceptable starting point (unlike a rule, which the
  benchmark must gate). A `reviewed:` front-matter flag marks unreviewed drafts
  (mirrors the authored-GT `reviewed` convention, ADR-032).
- **Hand-written rules** get an explainer the same way: `keur train --explain
  <RULE-ID>` generates the markdown for an existing rule from its `.rule` fields,
  so back-filling the current corpus is one command per rule, not manual prose.
- Deterministic fallback stays: with zero generated explainers, `keur learn`
  still prints title/message/fix/CWE from the `.rule` (§2), so nothing depends on
  generation having run.

This closes the loop: **`keur train` grows both the detector AND its
documentation from the same ground-truth step; `keur learn <RULE>` serves that
documentation to the human or AI that hit the finding.**

## Why not the alternatives

- **Keep everything under `learn`** (subcommands `learn train` / `learn rule`):
  more typing, and it perpetuates the "learn = train keur" confusion. The two
  verbs are genuinely different actions; two top-level verbs is clearer.
- **Embed the full explainer in the `.rule` file**: bloats every rule, mixes the
  matcher (data the engine reads) with prose (docs a human reads). Keep rules
  lean; explainers are separate markdown keyed by id.
- **Always fetch remote**: violates the offline/IP-stays-local principle and
  makes the hook depend on the network. Local-first, explicit `--fetch` only.

## Consequences

**Positive**
- The verbs finally match reality: `train` trains keur, `learn` teaches the user.
- A developer OR an AI that hits a BLOCK can `keur learn <RULE>` for the full
  rationale + safe/unsafe examples + how to fix/suppress — deepening ADR-035's
  self-explaining-findings promise.
- Explainers are versioned markdown in the repo (reviewable, PR-able, the same
  low-barrier contribution path as rules), fetched on demand, cached, offline-first.

**Negative / cost**
- A rename with a deprecation window (alias + note) touches `main.cpp` dispatch,
  `cmd/learn.*` (split into train + learn), the help text, and every doc/script
  that says `keur learn` for the loop (`docs/design/learning-loop.md`, README,
  `eval/supervise.sh` invocation, ADR-029/033/035 references).
- Explainer content is largely GENERATED by the train loop (§4), not hand-written
  — so it is not an ongoing manual backlog. The residual cost is (a) one extra
  prompt per accepted rule in the loop, and (b) reviewing generated drafts before
  they lose their `reviewed: false` flag. The deterministic fallback
  (title/message/fix/CWE from the `.rule`) means the command works from day one
  with zero explainers generated, and back-fill is `keur train --explain <RULE>`.

## Follow-ups

1. Split `cmd/learn.cpp` → `cmd/train.cpp` (the loop) + `cmd/learn.cpp` (the
   explainer fetch); wire `train` into `main.cpp`, keep `learn` alias → train with
   a deprecation note until `learn <RULE>` ships.
2. Add `docs/rules/_TEMPLATE.md`; wire explainer generation into the train loop's
   accept path (§4) and add `keur train --explain <RULE-ID>` to back-fill existing
   rules — starting with the session's new rules (KEUR-CMDI-003, KEUR-NOSQLI-001,
   KEUR-XSS-001, KEUR-INFO-001, KEUR-NEXT-001).
3. Add the `learn more: keur learn <RULE>` line to the BLOCK output (ADR-035
   formatter).
4. Update `docs/design/learning-loop.md` + README + `eval/supervise.sh` to say
   `keur train`.
