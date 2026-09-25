# Next.js Security Hardening — Antipatterns & Best Practices

Research feeding keur rule development. Scope: **Next.js** (App Router + Pages
Router). Each row pairs a concrete insecure pattern with its corresponding
hardening, cites an authoritative source (Next.js official docs, the Next.js
security blog / CVE postmortems, OWASP), and marks whether the pattern is
**statically detectable** in source code (so keur could write a `.rule`) or
only detectable at runtime/config.

Legend for "statically detectable":

- **yes** — a regex/AST rule over source or config can reliably flag it.
- **partial** — a rule can flag the *suspicious surface* (an API call, a config
  key, a directive) but cannot decide correctness; needs a heuristic + human
  confirmation, or produces a warn-level signal only.
- **no** — only observable at runtime, in HTTP responses, or requires
  cross-file dataflow/semantic reasoning beyond practical static rules.

Sources are canonical: `nextjs.org/docs`, the Next.js security blog
(`nextjs.org/blog`), Vercel's CVE postmortem, and OWASP Cheat Sheet Series /
Top 10. Concrete API names and config keys are used throughout. Nothing here is
invented; every row traces to a cited page.

> Terminology note: In Next.js 15.5+/16 the request-interception file was
> renamed from `middleware.ts` to `proxy.ts` (the CSP guide now shows `proxy`).
> Older projects and the CVE postmortem use `middleware`. Rules should match
> **both** `middleware.*` and `proxy.*`.

---

## Antipatterns → Best practices

| # | antipattern | corresponding best practice | source (name + URL) | statically detectable |
|---|-------------|-----------------------------|---------------------|-----------------------|
| 1 | No `headers()` block in `next.config.js` → app ships without any security response headers | Add an `async headers()` returning HSTS, X-Content-Type-Options, Referrer-Policy, Permissions-Policy, CSP, etc. | Next.js — Config `headers` (https://nextjs.org/docs/app/api-reference/config/next-config-js/headers) | partial (can flag absence of `headers()` in config) |
| 2 | Missing `Strict-Transport-Security` header | Set `Strict-Transport-Security: max-age=63072000; includeSubDomains; preload` | Next.js — headers §Strict-Transport-Security (https://nextjs.org/docs/app/api-reference/config/next-config-js/headers#strict-transport-security) | partial (flag absence in config `headers()`) |
| 3 | Missing `X-Content-Type-Options` → MIME sniffing / stored-XSS via uploaded files | Set `X-Content-Type-Options: nosniff` | Next.js — headers §X-Content-Type-Options (https://nextjs.org/docs/app/api-reference/config/next-config-js/headers#x-content-type-options) | partial |
| 4 | No clickjacking protection (`X-Frame-Options`/`frame-ancestors`) | Set `X-Frame-Options: SAMEORIGIN` and/or CSP `frame-ancestors 'none'` | Next.js — headers §X-Frame-Options (https://nextjs.org/docs/app/api-reference/config/next-config-js/headers#x-frame-options) | partial |
| 5 | Missing `Referrer-Policy` → full URL (with tokens in query) leaked cross-origin | Set `Referrer-Policy: origin-when-cross-origin` (or stricter) | Next.js — headers §Referrer-Policy (https://nextjs.org/docs/app/api-reference/config/next-config-js/headers#referrer-policy) | partial |
| 6 | Missing `Permissions-Policy` → camera/mic/geolocation available to embedded content | Set `Permissions-Policy: camera=(), microphone=(), geolocation=(), browsing-topics=()` | Next.js — headers §Permissions-Policy (https://nextjs.org/docs/app/api-reference/config/next-config-js/headers#permissions-policy) | partial |
| 7 | No Content-Security-Policy at all | Define a CSP header (via `next.config.js` `headers()` or `proxy.ts`) | Next.js — Content Security Policy (https://nextjs.org/docs/app/guides/content-security-policy) | partial (flag absence of any CSP source) |
| 8 | CSP uses `script-src 'unsafe-inline'` in production | Use nonce-based CSP (`'nonce-<value>' 'strict-dynamic'`) or SRI hashes | Next.js — CSP §Without Nonces / §Nonces (https://nextjs.org/docs/app/guides/content-security-policy#nonces) | yes (`'unsafe-inline'` literal in CSP string/config) |
| 9 | CSP ships `'unsafe-eval'` unconditionally (not gated on dev) | Gate `'unsafe-eval'` behind `NODE_ENV === 'development'`; omit in production | Next.js — CSP §Development vs Production (https://nextjs.org/docs/app/guides/content-security-policy#development-vs-production-considerations) | yes (`'unsafe-eval'` not wrapped in isDev check) |
| 10 | CSP omits `object-src` → plugin/Flash/embed injection | Include `object-src 'none'` | Next.js — CSP example policy (https://nextjs.org/docs/app/guides/content-security-policy#without-nonces) | partial (parse CSP string for missing directive) |
| 11 | CSP omits `base-uri` → `<base>` tag hijacking of relative URLs | Include `base-uri 'self'` | Next.js — CSP example policy (https://nextjs.org/docs/app/guides/content-security-policy#without-nonces) | partial |
| 12 | CSP omits `frame-ancestors` → clickjacking | Include `frame-ancestors 'none'` (supersedes X-Frame-Options) | Next.js — CSP example / headers §X-Frame-Options (https://nextjs.org/docs/app/api-reference/config/next-config-js/headers#x-frame-options) | partial |
| 13 | CSP omits `form-action` → forms post to attacker origin | Include `form-action 'self'` | Next.js — CSP example policy (https://nextjs.org/docs/app/guides/content-security-policy#without-nonces) | partial |
| 14 | CSP omits `upgrade-insecure-requests` → mixed content over HTTP | Include `upgrade-insecure-requests` | Next.js — CSP example policy (https://nextjs.org/docs/app/guides/content-security-policy#without-nonces) | partial |
| 15 | Static (`next.config`) CSP nonce reused across requests | Generate a fresh nonce **per request** in `proxy.ts`/middleware | Next.js — CSP §Why use a nonce (https://nextjs.org/docs/app/guides/content-security-policy#why-use-a-nonce) | partial |
| 16 | Predictable / hardcoded CSP nonce value | Use unpredictable `crypto.randomUUID()`-based base64 nonce | Next.js — CSP §Adding a nonce with Proxy (https://nextjs.org/docs/app/guides/content-security-policy#adding-a-nonce-with-proxy) | yes (hardcoded `'nonce-...'` string literal) |
| 17 | `poweredByHeader` left enabled → `X-Powered-By: Next.js` fingerprinting | Set `poweredByHeader: false` in `next.config.js` | Next.js — Config `poweredByHeader` (https://nextjs.org/docs/app/api-reference/config/next-config-js/poweredByHeader) | yes (flag absence of `poweredByHeader:false`) |
| 18 | `dangerouslySetInnerHTML={{ __html: userInput }}` with unsanitized data → XSS | Avoid it; if unavoidable, sanitize with DOMPurify before rendering | OWASP — XSS Prevention Cheat Sheet (https://cheatsheetseries.owasp.org/cheatsheets/Cross_Site_Scripting_Prevention_Cheat_Sheet.html) | yes (`dangerouslySetInnerHTML` occurrence) |
| 19 | Secret placed in `NEXT_PUBLIC_*` env var → inlined into client bundle at build | Keep secrets in non-prefixed env vars, read only server-side | Next.js — Env Vars §Bundling for the Browser (https://nextjs.org/docs/app/guides/environment-variables#bundling-environment-variables-for-the-browser) | yes (`NEXT_PUBLIC_` name containing SECRET/KEY/TOKEN/PASSWORD) |
| 20 | Assuming `NEXT_PUBLIC_*` is runtime-configurable | Understand it is **frozen at build time**; use a runtime API/`connection()` for runtime values | Next.js — Env Vars §Runtime Environment Variables (https://nextjs.org/docs/app/guides/environment-variables#runtime-environment-variables) | partial |
| 21 | Passing whole DB row / `User` object as a prop to a Client Component → over-exposure | Return a minimal Data Transfer Object (DTO) with only needed fields | Next.js Blog — Security §Component Level Data Access (https://nextjs.org/blog/security-nextjs-server-components-actions#component-level-data-access) | partial (Client Component prop typed as broad object) |
| 22 | Server Component passes `token`/`creditCard`/`phoneNumber` props to a Client Component | Never pass sensitive fields across the `'use client'` boundary | Next.js Blog — Security §Component Level Data Access (https://nextjs.org/blog/security-nextjs-server-components-actions#component-level-data-access) | partial (prop names token/secret/password near `use client`) |
| 23 | Sensitive/server-only module imported into a Client Component | Mark server-only modules with `import 'server-only'` (build fails on client import) | Next.js Blog — Security §Server Only (https://nextjs.org/blog/security-nextjs-server-components-actions#server-only) | yes (flag server-only module absence of the marker in a data-access dir) |
| 24 | `process.env.SECRET` accessed outside the Data Access Layer | Access `process.env` secrets only inside the Data Access Layer | Next.js Blog — Security §Data Access Layer (https://nextjs.org/blog/security-nextjs-server-components-actions#data-access-layer) | partial (`process.env.` usage in client-tagged files = high signal) |
| 25 | Server Action (`'use server'`) with no authorization check | Re-authorize the current user at the start of every action | Next.js Blog — Security §Write (https://nextjs.org/blog/security-nextjs-server-components-actions#write) | partial (`'use server'` fn without getCurrentUser/auth call) |
| 26 | Server Action trusts TypeScript types for args (no runtime validation) | Validate each argument at runtime (e.g. `zod`) — types are not enforced | Next.js Blog — Security §Write (https://nextjs.org/blog/security-nextjs-server-components-actions#write) | partial |
| 27 | Server Action closures via `.bind(null, secret)` assuming encryption | Use closure variables (encrypted) not `.bind()` for sensitive data — **`.bind()` is NOT encrypted** | Next.js Blog — Security §Closures (https://nextjs.org/blog/security-nextjs-server-components-actions#closures) | partial (`.bind(` on a `'use server'` action) |
| 28 | Treating middleware as the sole route-protection mechanism | Enforce authorization in the Data Access Layer / per-request, not only middleware | Vercel — CVE-2025-29927 postmortem §Technical analysis (https://vercel.com/blog/postmortem-on-next-js-middleware-bypass) | no (architectural / cross-file) |
| 29 | Running a Next.js version vulnerable to `x-middleware-subrequest` bypass (CVE-2025-29927) | Upgrade to ≥14.2.25 / ≥15.2.3 (or backports 13.5.9 / 12.3.5); strip `x-middleware-subrequest` at the edge | Vercel — CVE-2025-29927 postmortem (https://vercel.com/blog/postmortem-on-next-js-middleware-bypass) | partial (pin `next` version in package.json) |
| 30 | Middleware auth built as a **deny-list** (block known-bad paths) | Use an **allow-list** — enumerate what is permitted, deny by default | Next.js Blog — Security §Custom Routes and Middleware (https://nextjs.org/blog/security-nextjs-server-components-actions#custom-routes-and-middleware) | no |
| 31 | Middleware matcher forgets RSC/JSON payload routes (`?_rsc=`), protecting only HTML | Rely on the plain page URL for all navigations; audit matchers for RSC paths | Next.js Blog — Security §Custom Routes and Middleware (https://nextjs.org/blog/security-nextjs-server-components-actions#custom-routes-and-middleware) | no |
| 32 | Custom `route.tsx` GET handler performs mutations / side-effects | Never mutate on GET; use Server Actions (POST) for writes | Next.js Blog — Security §Read (https://nextjs.org/blog/security-nextjs-server-components-actions#read) | partial (GET export in route.ts doing writes) |
| 33 | Custom Route Handler with no manual CSRF protection | Server Actions have built-in CSRF/Origin checks; route handlers must add their own | Next.js Blog — Security §CSRF (https://nextjs.org/blog/security-nextjs-server-components-actions#csrf) | no |
| 34 | Trusting `searchParams`/dynamic `[param]` as authorization (e.g. `?isAdmin=true`, `/[team]/`) | Re-verify access control and read `cookies()` on every data read | Next.js Blog — Security §Read (https://nextjs.org/blog/security-nextjs-server-components-actions#read) | partial (searchParams used in an auth decision) |
| 35 | Route Handler fetches a URL taken from user input → SSRF | Validate/allow-list the destination host before `fetch()` | OWASP — SSRF Prevention Cheat Sheet (https://cheatsheetseries.owasp.org/cheatsheets/Server_Side_Request_Forgery_Prevention_Cheat_Sheet.html) | partial (`fetch(` with request-derived URL in route.ts) |
| 36 | `redirect()` / `NextResponse.redirect()` with a user-controlled target → open redirect | Allow-list redirect targets; only permit relative/same-origin paths | OWASP — Unvalidated Redirects & Forwards Cheat Sheet (https://cheatsheetseries.owasp.org/cheatsheets/Unvalidated_Redirects_and_Forwards_Cheat_Sheet.html) | partial (`redirect(searchParams...)`/`redirect(req...)`) |
| 37 | `next/image` `remotePatterns` omits `pathname`/`search` (wildcard `**` implied) | Be specific: set `pathname` and `search` to prevent optimizing unintended URLs | Next.js — Image §remotePatterns (https://nextjs.org/docs/app/api-reference/components/image#remotepatterns) | partial (remotePatterns entry missing pathname/search) |
| 38 | Using deprecated `images.domains` allowlist | Migrate to strict `images.remotePatterns` (supports protocol/port/path/wildcards) | Next.js — Image §domains (deprecated) (https://nextjs.org/docs/app/api-reference/config/next-config-js/images) | yes (`domains:` key under `images`) |
| 39 | Overly broad image host allowlist (`hostname: '**'` / wildcard middle) | Constrain to exact host / specific subdomain patterns | Next.js — Image §remotePatterns (https://nextjs.org/docs/app/api-reference/components/image#remotepatterns) | yes (`hostname: '**'` or `'*'`) |
| 40 | `images.dangerouslyAllowSVG: true` without a restrictive CSP → SVG-embedded script XSS | If SVG needed, also set `contentDispositionType: 'attachment'` and `contentSecurityPolicy: "default-src 'self'; script-src 'none'; sandbox;"` | Next.js — Image §dangerouslyAllowSVG (https://nextjs.org/docs/app/api-reference/components/image#dangerouslyallowsvg) | yes (`dangerouslyAllowSVG:true` present) |
| 41 | `images.dangerouslyAllowLocalIP: true` → SSRF to internal network | Leave `false` unless split-horizon DNS requires it and SSRF risk is understood | Next.js — Image §dangerouslyAllowLocalIP (https://nextjs.org/docs/app/api-reference/components/image#dangerouslyallowlocalip) | yes (`dangerouslyAllowLocalIP:true`) |
| 42 | `images.localPatterns` entry omits `search` → all query strings allowed | Set a specific `search` value (e.g. `'?v=2'`) for exact match | Next.js — Image §localPatterns (https://nextjs.org/docs/app/api-reference/components/image#localpatterns) | partial (localPatterns entry missing search) |
| 43 | Raw SQL string built with user input in a Server Component/Action → SQL injection | Use parameterized/templated queries (tagged `sql\`...${x}\``) | Next.js Blog — Security §Component Level Data Access (https://nextjs.org/blog/security-nextjs-server-components-actions#component-level-data-access) | partial (string-concatenated SQL) |
| 44 | Running production traffic with `next dev` / dev mode | Always run production builds in production; dev leaks plaintext errors & stack traces | Next.js Blog — Security §Error Handling (https://nextjs.org/blog/security-nextjs-server-components-actions#error-handling) | no (runtime/deploy) |
| 45 | `CORS Access-Control-Allow-Origin: "*"` on API/route handlers | Set a specific trusted origin, not `*` | Next.js — headers §CORS (https://nextjs.org/docs/app/api-reference/config/next-config-js/headers#cors) | yes (`Access-Control-Allow-Origin` value `*`) |
| 46 | Relying on the default auto-generated Server Actions encryption key across builds/instances | Set `NEXT_SERVER_ACTIONS_ENCRYPTION_KEY` for persistent/rotated key in multi-instance deploys | Next.js Blog — Security §Closures (https://nextjs.org/blog/security-nextjs-server-components-actions#closures) | partial (multi-instance + no env key set) |
| 47 | Not using the React Taint APIs to guard accidental data exposure | Enable `experimental.taint` and `taintObjectReference`/`taintUniqueValue` on sensitive data | Next.js Blog — Security §Server Only (https://nextjs.org/blog/security-nextjs-server-components-actions#server-only) | partial (taint config absent) |
| 48 | `env` block in `next.config.js` used to expose server secrets to the client bundle | Do not put secrets in `next.config.js` `env`; use server-only `process.env` | Next.js — Config `env` (https://nextjs.org/docs/app/api-reference/config/next-config-js/env) | partial (secret-looking keys under `env:`) |
| 49 | Error messages / stack traces returned to client contain sensitive data | Rely on production React error hashing; never echo secret values in thrown errors | Next.js Blog — Security §Error Handling (https://nextjs.org/blog/security-nextjs-server-components-actions#error-handling) | no |
| 50 | Mixing data-handling models (HTTP APIs + DAL + component-level) ad hoc | Pick one model per project so anomalies stand out to auditors | Next.js Blog — Security §Choosing Your Data Handling Model (https://nextjs.org/blog/security-nextjs-server-components-actions#choosing-your-data-handling-model) | no |
| 51 | `productionBrowserSourceMaps: true` → ships readable source maps exposing logic | Leave disabled (default) in production unless deliberately needed | Next.js — Config `productionBrowserSourceMaps` (https://nextjs.org/docs/app/api-reference/config/next-config-js/productionBrowserSourceMaps) | yes (`productionBrowserSourceMaps:true`) |
| 52 | Following image-optimization redirects unbounded (SSRF pivot) | Constrain `images.maximumRedirects` (0 disables) | Next.js — Image §maximumRedirects (https://nextjs.org/docs/app/api-reference/components/image#maximumredirects) | partial |
| 53 | Committing `.env*` files with real secrets | Keep `.env*` in `.gitignore` (create-next-app does this by default) | Next.js — Env Vars §Warning (https://nextjs.org/docs/app/guides/environment-variables) | yes (`.env` with secret-looking values tracked/staged) |

---

## Best-practice checklist (condensed, for rule triage)

Grouped by keur detectability, so rule authors can prioritise the **yes** rows.

### Statically detectable — strong candidates for `.rule`

- `poweredByHeader: false` present in `next.config.*` (row 17).
- No `NEXT_PUBLIC_` var whose name contains `SECRET|KEY|TOKEN|PASSWORD|PRIVATE`
  (row 19).
- No `dangerouslySetInnerHTML` (row 18) — warn, needs sanitizer check nearby.
- CSP string does not contain `'unsafe-inline'` in production (row 8) or
  ungated `'unsafe-eval'` (row 9); no hardcoded `'nonce-...'` literal (row 16).
- `images.domains` deprecated key not used (row 38); `hostname` not `'*'`/`'**'`
  (row 39); `dangerouslyAllowSVG` not true without CSP siblings (row 40);
  `dangerouslyAllowLocalIP` not true (row 41).
- `Access-Control-Allow-Origin` not `"*"` (row 45).
- `productionBrowserSourceMaps` not `true` (row 51).
- `import 'server-only'` present in server-only/data-access modules (row 23).
- Staged `.env*` file with secret-shaped values (row 53).

### Partial — warn-level heuristics (suspicious surface, needs human/context)

- `headers()` block absent / missing individual security headers (rows 1–7,
  10–14).
- `'use server'` function with no visible auth/validation call (rows 25, 26).
- `.bind(` on a Server Action (row 27).
- `fetch(` / `redirect()` with request-derived argument (rows 35, 36).
- `remotePatterns`/`localPatterns` entry missing `pathname`/`search` (rows 37,
  42).
- `process.env.` in a `'use client'`-tagged file (row 24).
- Broad prop types or sensitive prop names crossing the client boundary
  (rows 21, 22).
- String-concatenated SQL in server code (row 43).
- `next` version pinned below the CVE-2025-29927 patched releases (row 29).

### Not statically detectable — document/runtime controls only

- Middleware-as-sole-auth, allow-list vs deny-list, RSC matcher gaps (rows 28,
  30, 31).
- Route-handler CSRF, GET-mutation semantics beyond a simple export (rows 33,
  partial 32).
- Dev-mode in production, error-message leakage, data-model consistency
  (rows 44, 49, 50).

---

## Source index

- Next.js — Config `headers` (all security header subsections):
  https://nextjs.org/docs/app/api-reference/config/next-config-js/headers
- Next.js — Config `poweredByHeader`:
  https://nextjs.org/docs/app/api-reference/config/next-config-js/poweredByHeader
- Next.js — Config `images` (remotePatterns, domains, dangerouslyAllowSVG,
  dangerouslyAllowLocalIP, localPatterns, maximumRedirects) and Image component:
  https://nextjs.org/docs/app/api-reference/components/image
- Next.js — Config `env`:
  https://nextjs.org/docs/app/api-reference/config/next-config-js/env
- Next.js — Config `productionBrowserSourceMaps`:
  https://nextjs.org/docs/app/api-reference/config/next-config-js/productionBrowserSourceMaps
- Next.js — Guide: Content Security Policy:
  https://nextjs.org/docs/app/guides/content-security-policy
- Next.js — Guide: Environment Variables:
  https://nextjs.org/docs/app/guides/environment-variables
- Next.js Blog — How to Think About Security in Next.js (Server Components &
  Server Actions), Sebastian Markbåge:
  https://nextjs.org/blog/security-nextjs-server-components-actions
- Vercel — Postmortem on Next.js Middleware bypass (CVE-2025-29927):
  https://vercel.com/blog/postmortem-on-next-js-middleware-bypass
- GitHub Advisory GHSA-f82v-jwr5-mffw (CVE-2025-29927):
  https://github.com/advisories/GHSA-f82v-jwr5-mffw
- OWASP — Cross Site Scripting Prevention Cheat Sheet:
  https://cheatsheetseries.owasp.org/cheatsheets/Cross_Site_Scripting_Prevention_Cheat_Sheet.html
- OWASP — Server Side Request Forgery Prevention Cheat Sheet:
  https://cheatsheetseries.owasp.org/cheatsheets/Server_Side_Request_Forgery_Prevention_Cheat_Sheet.html
- OWASP — Unvalidated Redirects and Forwards Cheat Sheet:
  https://cheatsheetseries.owasp.org/cheatsheets/Unvalidated_Redirects_and_Forwards_Cheat_Sheet.html
