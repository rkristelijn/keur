# ADR-049 — Base64 unwrap-then-scan engine (surface secrets hidden in encoded blobs)

@id ADR-049
**Status**: accepted (implemented) · **Date**: 2026-10-08 · **Coverage**: engines `base64-unwrap`, `urldecode-unwrap`, `hex-unwrap` (src/rules/rule/engines.cpp); tests `[feature:rule-engine] *-unwrap *` in keur_test.cpp; rule SECRETS-090 · **Relates to**: ADR-001 (RE2 line engine — stays dumb), ADR-009 (engine stays executive; logic in rules/engines), ADR-014 (engine strategy-registry — this lands as a registered engine), ADR-019 (external scanners — why we prefer a tool flag first), ADR-042 (pii-vault-scanner — a prior standalone scanner this mirrors)

## Context

A real finding motivated this. A Cloudflare tunnel connector token was committed
in plaintext in an IaC repo:

```sh
cloud service install eyJhIjoi...   # base64( {"a":...,"t":...,"s":<secret>} )
```

Two independent, proven secret scanners missed it on real data (verified
2026-10-08):

- **gitleaks** (default rules): `no leaks found`.
- **trufflehog**: `0 verified, 0 unverified`.

The reason is structural, not a rule gap: the secret is **wrapped in a base64
blob**. The scanners match on the decoded shape of known providers (AWS `AKIA`,
GitHub `ghp_`, …). A base64 envelope hides the inner `{"s":<secret>}` from a
surface match. The sensitive material is one decode away, and nothing decodes it.

This generalises beyond Any secret or PII carried inside a base64
(or base64url) field, a connector token, an encoded config value, a data URI,
is invisible to a surface scan. That is a recurring blind spot, not a one-off.

The naive fix, "decode `eyJ…` then look inside", must not go into the RE2 line
engine. ADR-009's invariant is explicit: the engine stays dumb and executive;
transforms and procedural logic do not live in the matcher. A regex cannot (and
must not) base64-decode and re-scan. So the capability belongs in the **scanner
/ engine orchestration layer** (ADR-014), as a registered engine, not as a rule.

## Decision

Add a **base64 unwrap-then-scan engine**: a deterministic transform scanner that
finds base64 blobs, decodes one layer, and re-runs the existing secret/PII
detection over the decoded bytes. It is a new engine in the registry (ADR-014),
in the `secrets` / `pii` groups, never a change to the RE2 engine.

### Pipeline (one pass, linear, bounded)

@id ADR-049.pipeline

1. **Find** — scan the staged diff for base64 candidates: runs of
   `[A-Za-z0-9+/=_-]` of length ≥ N (default 24). Optionally boost candidates
   with a JSON hint (`eyJ` = `{"`), but do not require it.
2. **Filter** — cheap pre-checks before decoding, to keep noise and cost down:
   valid base64 length/padding; decode must yield valid UTF-8 (drop binary:
   images, hashes, compiled blobs); optionally require the decoded bytes to look
   structured (JSON `{`/`[`, or `key=value`).
3. **Decode** — base64 / base64url decode, exactly **one layer** by default
   (see Scope & limits).
4. **Re-scan** — feed decoded bytes back through the existing secret/PII
   detectors (gitleaks/trufflehog patterns + keur rules). Reuse, do not
   reimplement, detection.
5. **Report** — the finding points at the **original encoded span** in the file,
   annotated with what the decode revealed. Never echo the raw inner secret in
   output; redact (consistent with the vault/PII handling in ADR-042/ADR-046).

### Determinism & cost

@id ADR-049.determinism

- One decode layer by default. Depth is a config knob, capped (e.g. ≤ 2); no
  unbounded recursion — that is where linear-time and determinism die.
- Candidate length floor + UTF-8 filter keep the decode set small.
- No network, no external state. Same input → same verdict (keur invariant).

## Try the cheap thing first

@id ADR-049.tool-first

Per ADR-019 (prefer proven tools over our own engine), before building this:
**test `gitleaks --decode`.** gitleaks can decode base64 layers and scan the
output. If that single flag catches the cloudflared token on the real repo, the
"engine" is one line of scanner config, not new code, and this ADR is parked as
"covered by tool flag". This engine is only justified if the tool flags leave a
real, demonstrated gap (e.g. gitleaks `--decode` misses it, or we need PII, not
just secrets, inside the blob).

## Scope & limits

- **One layer by default.** base64-in-base64 is possible but rare; depth is
  capped and opt-in. Unbounded unwrapping is explicitly out.
- **UTF-8 / structured only.** Blobs that decode to binary are dropped. We are
  hunting secrets/PII in encoded text, not scanning arbitrary binary.
- **Not a replacement** for the surface scanners. This is an additive layer for
  the wrapped case; the proven tools remain the primary layer (ADR-009 framing:
  our engine is a gap-filler).
- **False-positive risk is real.** JWTs, SRI hashes, data URIs, minified assets
  are all long base64. The filter step (valid base64 + UTF-8 + structure) is the
  main defence; tune N and the structure check against the synthetic corpus
  (ADR-023) before enabling by default.

## Consequences

**Positive**
- Closes a demonstrated blind spot: secrets/PII one base64 layer deep, which the
  two proven secret tools miss.
- Lands cleanly as a registered engine (ADR-014); the RE2 engine and ADR-009
  invariant are untouched.
- Reuses existing detection (no second secret ruleset to maintain).

**Negative / risk**
- New engine = new code, tests, and corpus validation (ADR-023, ADR-031).
- False positives on legitimate base64 if the filter is loose; must be tuned and
  measured (ADR-027) before default-on.
- Cost per scan rises (decode + re-scan of candidates); mitigated by the
  candidate filter and one-layer cap, and measured against ADR-024/025.

**Rollout**
- Default **off** until corpus FP rate is acceptable; opt-in via scanner config.
- Ship with a token corpus case (prefix `eyJhIjoi…` = `{"a":"…`) so
  the originating finding is a regression test.

## Implementation status (2026-10-08)

Implemented as three registered per-line engines sharing one `unwrap_scan`
driver (candidate-find → decode one layer → UTF-8/text filter → re-scan the
decoded text with the rule's own patterns → report at the ENCODED line, never
echo the decoded value):

- `base64-unwrap` — base64 / base64url, padding optional. Default length floor
  24 (`min_count`). Ships with rule SECRETS-090 (cloudflared token + generic
  credential shapes + AWS key).
- `urldecode-unwrap` — percent-decode (`%XX`, `+`→space). Default floor 8;
  requires at least one real `%XX` escape so plain text never triggers a decode.
- `hex-unwrap` — bare even-length hex run → bytes. Default floor 16. The UTF-8
  filter drops hashes/binary, which is the main FP defence here. Distinct from
  SCA-066, which matches `\xNN` ESCAPE sequences in source, not a raw hex blob.

Detection stays in the rule's `patterns:` (data); the engines only perform the
decode transform (ADR-009 invariant intact). One layer, no recursion, no network
— deterministic.

## Future / not yet covered (nog te doen)

Deliberately out for now; revisit if a real finding justifies the FP cost:

- **base32** — used by some tokens / TOTP seeds. Same driver shape; add a
  `base32-unwrap` engine when there is a demonstrated case.
- **Nested / multi-layer** (base64-in-base64, url-in-base64) — the driver caps
  at one layer. A bounded `depth` knob (≤ 2) is a small addition but needs
  corpus FP measurement (ADR-023/027) first.
- **gzip/zlib-in-base64** — a compressed inner payload. Out of the "text only"
  scope by design; would need a bounded inflate step and a size cap.
- **ROT13 / caesar / fixed-key XOR** — malware obfuscation. High FP risk, low
  base rate; not worth it without a concrete case.

## Open questions

- Does `gitleaks --decode` already cover the secret case? (ADR-049.tool-first —
  the engine is justified because it also handles PII and url/hex, not just the
  secret-via-gitleaks case; keep as the gap-filler per ADR-009.)
- base64url (`-`/`_`) and missing-padding variants: **handled** (shared alphabet
  table, padding stripped before decode).
- Where does the decoded re-scan run? **Resolved**: a dedicated set of registered
  engines (ADR-014), so reporting points at the encoded span.
