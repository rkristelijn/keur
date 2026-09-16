# HOWTO — build and maintain the keur-eval corpus

`keur-eval` is KEUR's ground truth: an immutable set of **fictional**, annotated
test cases used to score the detector (precision/recall/regressions). This guide
covers generating, reviewing, and indexing cases.

> Golden rule: **every case is fictional.** No real secrets, PII, customers, or
> internal hostnames. Use obvious markers — `FICTIONAL`, `EXAMPLE`, `0000`,
> `acme-corp`, RFC-5737 IPs (`192.0.2.x`), RFC-2606 domains (`example.com`).
> Secrets must be structurally valid but dead (fire a detector, authenticate
> nothing). Full contract: `docs/design/keur-eval-convention.md`.

## Layout

```text
keur-eval/
  positive/<category>/    cases that MUST be detected
  negative/<category>/    look-alikes that must NOT be flagged
  regression/<category>/  previously-fixed failures, kept so they never return
  categories.yaml         categories + generation prompts
  index.json              machine-readable map the evaluator reads
```

## The annotation contract

Every case file starts with a comment (style adapts to the language):

```text
KEUR-EXPECT: SECRET-AWS-001            # positive: this rule must fire
KEUR-EXPECT: none                      # negative: nothing must fire
KEUR-EXPECT-NOT: SECRET-AWS-001        # this rule must NOT fire (was a false positive)
KEUR-CATEGORY: secrets
KEUR-NOTE: why this case exists
```

## 1. Generate draft cases (local LLM)

Cases are drafted by a **local** LLM (Ollama) from `categories.yaml`, then
reviewed by a human. Nothing leaves the machine.

```bash
# one category, N cases
eval/gen-cases.sh secrets 5
eval/gen-cases.sh --negative secrets-safe 3

# all categories, in the background (per-category count, rounds)
eval/gen-batch.sh 5 4
#   log:  /tmp/genbatch.out
#   stop: touch /tmp/stop-genbatch
```

Requirements: a running local Ollama (`ollama serve`) with the model pulled,
`yq`, `curl`, `python3`. Model via `MODEL=…` (default is a mid-size local coder
model).

## 2. Review (required — do not skip)

The generator produces **drafts** (each `KEUR-NOTE` says REVIEW). LLM drafts have
predictable weaknesses; check before committing:

- **Positives must contain something detectable.** Reject placeholder values like
  `XXXX-XXXX` — use structurally valid fictional data instead.
- **Negatives must be true look-alikes** of the target rule, not off-topic. (A
  prompt saying "secret" can produce spy fiction instead of a credential
  look-alike — reject those.)
- **Data really is fictional** (example.com, 192.0.2.x, acme-corp, dead keys).
- **The `KEUR-EXPECT` line matches the content** (right rule-IDs / `none`).

Delete or fix weak cases. The corpus quality is the ceiling on every metric KEUR
ever reports — a case you don't trust makes the score meaningless.

## 3. Rebuild the index

The evaluator reads `index.json`, not the files. Regenerate it after adding or
editing cases:

```bash
eval/build-index.sh
```

This walks all cases, reads their `KEUR-EXPECT` annotations, and writes
`index.json` (`{path, class, category, expect, expect_not}` per case).

## 4. Splits (prevent overfitting)

Keep a held-out slice. `train/` and `validation/` may guide rule development; the
final `evaluation/` and `regression/` slices must never be shown to any optimiser
— otherwise the detector can "learn to pass its own tests."

## Adding a new category

Edit `categories.yaml`: add an entry under `categories:` (positive) or
`negatives:` with `id`, `domain`, `rules` (or `expect_not`), `langs`, and a
`prompt` describing the fictional content. Then run `gen-cases.sh <id>` and
review.

## Rule-ID naming

`<DOMAIN>-<SUBTYPE>-<NNN>`, uppercase, stable. Once a rule-ID is used in the
corpus, don't rename it (only add). It's the contract between corpus and detector.
