# keur-eval convention — the evaluation corpus baseline

> The immutable ground-truth corpus for KEUR lives in the sibling repo
> `keur-eval`. This document is the CONVENTION every case in that repo must
> follow, so the evaluator can score KEUR deterministically. All data is
> FICTIONAL and hand/LLM-authored — never real secrets, PII, or proprietary data.

## Why a separate corpus

KEUR (the detector) must never be able to modify its own ground truth. The
corpus is authored independently, versioned, and treated as immutable during any
optimisation. A rule you cannot measure against fixed cases is a rule you cannot
trust.

## Directory layout

```text
keur-eval/
  positive/     # cases that MUST be detected (a defect/sensitive item is present)
  negative/     # cases that must NOT be flagged (look-alikes, safe patterns)
  regression/   # previously-fixed failures, kept so they never return
  index.json    # machine-readable map of every case → expected flags
  README.md     # points here; how to add a case
```

Under each of the three, group by CATEGORY (e.g. `positive/secrets/`,
`positive/pii/`, `negative/pii/`). Category list lives in `categories.yaml`.

## The KEUR-EXPECT annotation (the core contract)

Every case file declares, in a comment near the top, what KEUR is expected to
find. This makes the corpus self-describing and machine-checkable.

Syntax (comment style adapts to the file's language):

```text
# KEUR-EXPECT: <RULE-ID>[@<line>] [<RULE-ID> ...]   # positive: these must fire
# KEUR-EXPECT: none                                  # negative: nothing must fire
# KEUR-EXPECT-NOT: <RULE-ID>                          # this rule must NOT fire (was a FP)
# KEUR-CATEGORY: <category>
# KEUR-NOTE: <free text, why this case exists>
```

Examples:

```python
# KEUR-EXPECT: SECRET-AWS-001
# KEUR-CATEGORY: secrets
# KEUR-NOTE: fictional AWS key literal in source
AWS_KEY = "AKIAFICTIONAL0000EXAMPLE"
```

```python
# KEUR-EXPECT: none
# KEUR-EXPECT-NOT: SECRET-AWS-001
# KEUR-CATEGORY: secrets
# KEUR-NOTE: env-var reference, not a real secret — must not be flagged
AWS_KEY = os.environ["AWS_KEY"]
```

## Rule-ID naming

`<DOMAIN>-<SUBTYPE>-<NNN>`, uppercase, stable. Domains (initial):
`SECRET, PII, CBI, SQLI, XSS, CMDI, DEPS, IAC, LICENSE`. The rule-ID is the
contract between corpus and detector; once used in the corpus it must not be
renamed (only added).

## index.json

Compact, generated from the annotations; the evaluator reads THIS, not the files:

```json
{
  "cases": [
    {"path": "positive/secrets/aws-key-literal.py",
     "class": "positive", "category": "secrets",
     "expect": ["SECRET-AWS-001"], "expect_not": []},
    {"path": "negative/secrets/aws-env-ref.py",
     "class": "negative", "category": "secrets",
     "expect": [], "expect_not": ["SECRET-AWS-001"]}
  ]
}
```

## Data rules (safety)

- **Fictional only.** No real keys, real people, real customers, real internal
  hostnames. Use obvious markers: `FICTIONAL`, `EXAMPLE`, `0000`, `acme-corp`,
  RFC-5737 IPs (192.0.2.x), RFC-2606 domains (example.com).
- Secrets must be **structurally valid but dead** (e.g. pass a regex/Luhn but
  never authenticate) so detectors fire without any real exposure.
- Every file carries a KEUR-EXPECT line; no unannotated cases.

## How the evaluator uses it (deterministic scoring)

For each case: run KEUR → compare fired rule-IDs against `expect` / `expect_not`.

- fired ∈ expect → TP; expected but not fired → FN; fired ∉ expect (or ∈
  expect_not) → FP; nothing expected and nothing fired → TN.
Aggregate → precision, recall, F1, per-category, plus the Pareto set (critical
FN, FP/KLOC, runtime). The `evaluation/` slice is never shown to any optimiser.

## Splits (prevent overfitting)

`train/` and `validation/` may guide rule development; the final `evaluation/`
and `regression/` slices are held out. Same KEUR-EXPECT convention throughout.
