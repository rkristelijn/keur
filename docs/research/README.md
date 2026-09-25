# Framework hardening research

Sourced antipatterns and best practices for the frameworks used in the `mestjs`
worst-repo, gathered to drive keur rule development. Each per-framework doc pairs
an **antipattern** with its **best practice**, cites an authoritative source
(OWASP cheat sheets, official framework security docs, maintainer guidance), and
rates whether the pattern is **statically detectable** in source (`yes` /
`partial` / `no`) — i.e. whether keur can realistically write a `.rule` for it.

## Documents

| Framework | Items | Doc |
|-----------|-------|-----|
| NestJS | 106 (54 antipatterns + 52 best practices) | [nestjs.md](./nestjs.md) |
| Nx | 63 | [nx.md](./nx.md) |
| Next.js | 53 | [nextjs.md](./nextjs.md) |
| SQLite / better-sqlite3 | 52 | [sqlite.md](./sqlite.md) |
| TanStack Query + Table | 45 | [tanstack.md](./tanstack.md) |

Total: ~319 sourced items.

## How this maps to keur

keur detects on **known patterns** with the RE2 engine (ADR-009), scoped by
**facts** (`requires: framework: <dep>`) so a NestJS rule only runs on a NestJS
project. The `statically detectable` column in each doc is the filter:

- **yes** → strong `.rule` candidate (pattern / presence / absence engine).
- **partial** → a heuristic shape is matchable but full correctness needs
  data-flow (accept some false positives, or gate with `requires:`/composite).
- **no** → runtime/config-only; out of scope for a static rule.

### Highest-signal static rule candidates (the "yes" shortlist)

These are the cleanest first `.rule` targets surfaced by the research. Several
directly close the false-negatives `mestjs` exposed.

- **NestJS** — missing `helmet()` in bootstrap (block-absence, `requires:
  framework: @nestjs`); missing `@nestjs/throttler` on auth routes; `enableCors({
  origin: '*' | true })`; `ValidationPipe` without `whitelist`/
  `forbidNonWhitelisted`; `$queryRawUnsafe` / `Prisma.raw` / concatenated
  `.query(...)`; hard-coded JWT secret / `ignoreExpiration: true`.
- **Next.js** — `poweredByHeader` not `false`; `next.config` without a
  `headers()` security-header set / CSP (absence, `requires: framework: next`);
  `dangerouslySetInnerHTML`; secrets named `NEXT_PUBLIC_*`; CSP with
  `'unsafe-inline'`/`'unsafe-eval'`; `images.domains` / hostname `'*'`;
  `dangerouslyAllowSVG`; `productionBrowserSourceMaps: true`.
- **SQLite** — raw SQL string concatenation into `prepare`/`exec`/`query`
  (extends KEUR-SQLI-001); `db.loadExtension`; `PRAGMA journal_mode = OFF` /
  `synchronous = OFF`; custom `.function()` without `directOnly`.
- **TanStack Query** — `new QueryClient()` in a component body; `retry: true`
  on auth flows; persisting the cache to `localStorage` without an allowlist;
  `refetchIntervalInBackground: true`.
- **TanStack Table** — `dangerouslySetInnerHTML` inside a cell/header renderer.
- **Nx** — committed `nxCloudAccessToken` / `accessToken` in `nx.json`;
  `@nx/enforce-module-boundaries` off/warn; wildcard `depConstraints`; secrets
  in `project.json`.

### Scope every framework rule with `requires:`

Framework-specific rules **must** carry a `requires: framework: <dep>` guard so
they only run where that framework is present (facts are derived from
`package.json` dependencies, substring-matched). Without the guard, an
absence-rule like "no helmet" would false-positive on every TypeScript project.
See `docs/adr/ADR-009-composite-rules-and-facts.md`.

## Provenance

Compiled from canonical sources fetched during research: OWASP Cheat Sheet
Series + OWASP Top 10 / API Top 10, the official docs for each framework
(docs.nestjs.com, nextjs.org, nx.dev, sqlite.org, better-sqlite3, tanstack.com),
and named maintainer guidance (TkDodo for react-query, eslint-plugin-nestjs-
security, the Nx s1ngularity postmortem). Per-row source URLs are in each doc's
source key. Nothing was invented; lists stop rather than pad where authoritative
backing was thin.
