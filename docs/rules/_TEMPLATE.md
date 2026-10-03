<!--
  Explainer template for a keur rule (ADR-036 §3).

  Copy to docs/rules/<RULE-ID>.md and fill in. `keur learn <RULE-ID>` resolves
  this file offline-first (local -> cache -> opt-in remote) and prints it raw.
  Keep it small and uniform: why it matters, what fires, what does not, the fix,
  how to suppress, and references.

  `reviewed: false` in the front matter marks an unreviewed draft (e.g. one the
  train loop generated). Flip to `reviewed: true` after a human/PR review.
-->
---
reviewed: false
---

# <RULE-ID> — <one-line title>
category: <category> · severity: <severity> · <CWE-nnn>

## Why this matters
<the risk in 2–3 sentences: what an attacker/mistake can do, and why the pattern
is dangerous in the general case>

## Unsafe (what fires)
```<lang>
<minimal example that the rule flags>
```

## Safe (what does not)
```<lang>
<the corrected form that does not fire>
```

## Fix
<the remediation — the rule's `fix:` field, expanded into a sentence or two>

## Suppress (if genuinely a false positive)
`# keur:ignore <RULE-ID>`  (or a `.keurignore` entry — ADR-035)

## References
- <CWE / OWASP / upstream advisory links>
