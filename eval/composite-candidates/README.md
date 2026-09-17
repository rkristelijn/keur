# composite-candidates

Rules the learn loop flagged as **slop** (they fire on a case but not on a
perturbed variant — they learned the FORM, not the CONCEPT). They are NOT
discarded and NOT loaded as standalone rules (that would add WARN noise).

They are kept here as **building blocks for future composite rules**: a weak
selector on its own (e.g. an `internal hostname:` label) becomes a reliable
signal when it CORROBORATES another weak selector (e.g. a private-IP pattern)
in the same file/block. See TODO.md → "composite rules via corroboration" and
the corroboration-bonus / escalate-band settings in config/keur.toml.

Each file keeps its original metadata (category, the case it fired on) so a
future composite builder can group them.
