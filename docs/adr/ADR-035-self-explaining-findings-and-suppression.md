# ADR-035: Self-explaining findings & layered suppression — keur as an AI feedback loop

@id ADR-035

## Status

Accepted · 2026-09-26 · Coverage: code
· Relates to: ADR-001 (RE2 engine), ADR-019 (scanner dispatch), ADR-032 (ground truth),
`docs/design/vision.md`, `docs/design/scope.md`, `eval/benchmark.sh`

## Context — the benchmark

We built a head-to-head benchmark (`eval/benchmark.sh` + `benchmark-score.py`)
of every detector against the AUTHORED ground truth (the corpus repos' own vuln
markers), measuring TP/FN/recall, false positives against author-safe labels,
raw finding volume, and — critically — **wall-clock time**. Results (2026-09-26,
116 author-marked vulnerable loci):

| detector | TP | recall | FP* | raw findings | corpus scan time |
|----------|---:|-------:|----:|-------------:|-----------------:|
| **keur** | 45 | **39%** | ~325 | 35 033 | **33 s** |
| semgrep  | 11 | 9% | 0 | 2 668 | **855 s** (14 min) |
| njsscan  | 3 | 3% | 0 | 343 | fast |
| bandit   | 2 | 2% | 0 | 2 462 | 20 s |
| gitleaks/trivy/grype/checkov/tfsec/hadolint | 0 | 0% | — | — | — (different specialisation) |

Two facts stand out:

1. **keur leads on recall AND speed by a wide margin.** 39% vs semgrep's 9%, in
   33 s vs semgrep's 14 minutes. For a pre-commit gate — keur's reason to exist —
   14 minutes per commit is unusable; 33 s is not.
2. **keur is far noisier.** ~325 hits on author-safe loci and 35 033 raw findings
   vs semgrep's 0 / 2 668. keur is a fast broad net; semgrep is a slow precise
   knife. On classic precision, keur loses.

The naive reading ("325 false positives is a disaster") assumes a **human**
committer who, annoyed by noise, runs `git commit --no-verify` and disables the
gate forever (the exact failure `scope.md`/the cpm lessons warn about). But
keur's stated purpose (`vision.md`) is to **contain AI-generated slop** via
**shift-left**. When the committer is an AI, a BLOCK is not friction — it is a
**feedback signal**: the AI reads the finding, looks again, and either fixes the
code or suppresses a genuine false positive. That only works if the finding
tells it exactly HOW.

## Reframe — what keur should optimise for

**keur is a fast, deterministic feedback loop for an AI (or human) committer, not
a precise oracle.** Its currency is (a) speed enough to run on every commit, and
(b) findings clear enough that the committer can act with zero外部 knowledge —
fix the real issue, or suppress a real false positive, without reaching for
`--no-verify`. Under this frame:

- **Recall + speed matter most** (keur wins both). A broad, fast net that a
  competent committer can act on beats a slow, narrow one that runs too late.
- **Precision matters, but the mitigation is a SELF-EXPLAINING finding + easy
  suppression, not fewer rules.** A false positive an AI can suppress in one
  auditable line is cheap; a false positive with no visible remedy is what
  gets the gate disabled.

This does not license noise — the learn loop (ADR-032) still gates rules on
precision. It reframes the *product*: keur's edge is speed + breadth + an
actionable error surface, explicitly NOT interprocedural depth (ADR-004).

## Decision

### 1. Findings are self-explaining (fix + suppress hint)

Every BLOCK/WARN line now prints, in addition to rule/file/line/message:

```
BLOCK KEUR-CMDI-003 | block | conf 0.85 | vuln.py:3 | <message> (CWE-78)
        fix: <the rule's fix: field — the remediation>
        false positive? add on that line a comment: keur:ignore KEUR-CMDI-003
```

- The `fix:` field already existed on every rule but never reached output; it is
  now carried through `RuleFinding` → `scan::Finding` → `format_finding_line`.
- The suppress hint names the exact inline marker. An AI needs no docs: the
  error is the manual.

### 2. Layered suppression (three layers, one honest model)

1. **Inline** `keur:ignore <RULE-ID>` on the offending line (in-diff, reviewable).
2. **`.keurignore`** file — the single standalone suppression file (like
   `.gitleaksignore` / `.semgrepignore`), resolved from the repo root then
   `.config/`. This is the preferred place for repo-level suppression.
3. **keur.toml `[rules] disabled = [...]`** — still honoured (filters keur-rules
   pre-scan and now also external-tool findings post-dispatch), but `.keurignore`
   is the recommended home so all suppression lives in one file.

### 3. Suppression is category-aware — the maintainer's rule

- **SECRETS suppress by VALUE, never by location.** `.keurignore` line
  `secret <value>` suppresses that specific secret anywhere. A moved secret
  stays suppressed; a **changed** secret is caught again (the value is the
  identity). A `path`/`line` entry MUST NOT silence a secret — keur keeps
  checking it. Verified: a `sec.py:KEUR-SEC-001` / `sec.py` entry does NOT
  suppress an AWS-key finding.
- **CODE issues suppress by LOCATION + rule.** `.keurignore` forms:
  `RULE-ID` (everywhere), `path` (under a path), `path:RULE-ID`,
  `path/file:LINE`, `path/file:LINE:RULE-ID`. There the code IS the value, so a
  location entry is correct.

## Consequences

**Positive**
- The BLOCK surface now delivers keur's core promise (`vision.md`: "clear errors
  so a human OR an AI knows exactly what to fix — no reaching for --no-verify").
- One suppression file (`.keurignore`), tool-conventional, with the correct
  semantics per category (secrets can't be accidentally silenced by location).
- keur's high recall + speed become a usable feedback loop rather than a noise
  complaint, matching the anti-slop / shift-left mission.

**Negative / honest limits**
- **Secret-by-value suppression needs the engine to EXPOSE the matched value**
  in the finding. Today findings carry `message`, not the raw matched secret, so
  `secret <value>` matches only when the value appears in the message. Exposing
  the matched capture (and a `sha256:` form so the plaintext secret need not sit
  in `.keurignore`) is a follow-up. The design and the safety rule (secrets never
  line-suppressed) are enforced now; the value plumbing is the remaining work.
- keur's precision is still its weak axis vs semgrep. The learn loop + rule
  tightening (not this ADR) own that.

## Follow-ups

1. Expose the matched value/capture on secret findings so `secret <value>` and
   `sha256:<hash>` suppression work exactly (not via message text).
2. `keur:ignore` audit trail → feed suppressed FPs back into keur-eval `expect_not`
   (ADR-032) so a real FP is measured and never returns.
3. Consider deprecating keur.toml `[rules] disabled` in favour of `.keurignore`
   once the latter covers every case (keep both for now).
