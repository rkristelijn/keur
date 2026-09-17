# imported rules

Pattern/presence/absence rules adopted verbatim from cpm
(github.com/rkristelijn/cpm, MIT). keur's .rule format is cpm-compatible, so
these load unchanged. Categories: secrets, security, supply-chain — the
security-relevant, high-signal families keur's own corpus did not yet cover.

Only engines keur supports are kept (extract-duplicates etc. are filtered out).
IDs keep their cpm names (stable contract). This is the "distillation over the
field" direction of ADR-004 in practice: reuse a proven tool's rules rather
than reinvent them.
