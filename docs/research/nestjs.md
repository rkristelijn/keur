# NestJS Security Hardening — Antipatterns & Best Practices

Research to feed keur rule development. Each row pairs an insecure pattern
(antipattern) with the corresponding hardening (best practice), cites an
authoritative source, and notes whether the pattern is **statically detectable**
in source code (so keur could write a `.rule`) — `yes`, `no` (runtime/config
only), or `partial` (a heuristic can flag the shape, but full correctness needs
runtime/data-flow).

Scope: NestJS (Express and Fastify adapters), `@nestjs/throttler`,
`class-validator` / `ValidationPipe`, CORS, guards, exception handling, CSRF,
mass assignment, `@nestjs/config`, TypeORM/Prisma raw queries, and JWT handling.

## Source key

- **NestJS Security — Helmet / security headers**: https://docs.nestjs.com/security/helmet
- **NestJS Security — CSRF**: https://docs.nestjs.com/security/csrf
- **NestJS Security — CORS**: https://docs.nestjs.com/security/cors
- **NestJS Security — Rate limiting**: https://docs.nestjs.com/security/rate-limiting
- **NestJS Security — Authentication**: https://docs.nestjs.com/security/authentication
- **NestJS — Pipes / ValidationPipe**: https://docs.nestjs.com/pipes and https://docs.nestjs.com/techniques/validation
- **NestJS — Guards**: https://docs.nestjs.com/guards
- **NestJS — Exception filters**: https://docs.nestjs.com/exception-filters
- **NestJS — Serialization (ClassSerializerInterceptor)**: https://docs.nestjs.com/techniques/serialization
- **NestJS — Configuration (`@nestjs/config`)**: https://docs.nestjs.com/techniques/configuration
- **eslint-plugin-nestjs-security** (Ofri Peretz): https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic and https://www.npmjs.com/package/eslint-plugin-nestjs-security
- **OWASP Node.js Security Cheat Sheet**: https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html
- **OWASP Mass Assignment Cheat Sheet**: https://cheatsheetseries.owasp.org/cheatsheets/Mass_Assignment_Cheat_Sheet.html
- **OWASP CSRF Prevention Cheat Sheet**: https://cheatsheetseries.owasp.org/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.html
- **OWASP JWT for Java Cheat Sheet** (the canonical OWASP JWT guidance): https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html
- **OWASP SQL Injection Prevention Cheat Sheet**: https://cheatsheetseries.owasp.org/cheatsheets/SQL_Injection_Prevention_Cheat_Sheet.html
- **OWASP Query Parameterization Cheat Sheet**: https://cheatsheetseries.owasp.org/cheatsheets/Query_Parameterization_Cheat_Sheet.html
- **OWASP Error Handling Cheat Sheet**: https://cheatsheetseries.owasp.org/cheatsheets/Error_Handling_Cheat_Sheet.html
- **OWASP Secrets Management Cheat Sheet**: https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html
- **OWASP Top 10 2021**: https://owasp.org/Top10/
- **OWASP API Security Top 10 2023**: https://owasp.org/API-Security/editions/2023/en/0x11-t10/
- **Prisma — Raw queries / SQL injection prevention**: https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries
- **TypeORM — Query Builder / SelectQueryBuilder (parameters)**: https://typeorm.io/select-query-builder
- **expressjs/cors — configuration options**: https://github.com/expressjs/cors#configuration-options

---

## 1. Antipatterns

| # | antipattern | corresponding best practice | source (name + URL) | statically detectable |
|---|-------------|-----------------------------|---------------------|-----------------------|
| 1 | No `useSecurityHeaders()` / `helmet` registered in `main.ts` — responses ship without CSP, HSTS, `X-Frame-Options`, etc. | Call `app.useSecurityHeaders()` (NestJS ≥ v12.1) or `app.use(helmet())` right after `NestFactory.create()` | NestJS Helmet — https://docs.nestjs.com/security/helmet | yes |
| 2 | `X-Powered-By: Express` header left enabled, leaking the framework/version | `useSecurityHeaders()` removes `X-Powered-By`; or `app.disable('x-powered-by')` / `helmet.hidePoweredBy()` | NestJS Helmet — https://docs.nestjs.com/security/helmet ; OWASP Node.js CS (X-Powered-By) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | yes |
| 3 | `useSecurityHeaders()` / helmet registered *after* `app.use(...)` route middleware, so those responses carry no headers | Register security headers immediately after `NestFactory.create()`, before any other `app.use()` | NestJS Helmet — https://docs.nestjs.com/security/helmet | partial |
| 4 | CSP `script-src` contains `'unsafe-inline'` in production, defeating XSS protection | Remove `'unsafe-inline'`; use nonces/hashes; keep the default restrictive CSP | NestJS Helmet (CSP warning) — https://docs.nestjs.com/security/helmet ; OWASP CSP CS — https://cheatsheetseries.owasp.org/cheatsheets/Content_Security_Policy_Cheat_Sheet.html | yes |
| 5 | `app.enableCors({ origin: '*' })` on an authenticated API | Set an explicit allow-list of origins; never `*` for credentialed APIs | NestJS CORS — https://docs.nestjs.com/security/cors ; expressjs/cors — https://github.com/expressjs/cors#configuration-options | yes |
| 6 | `app.enableCors({ origin: true, credentials: true })` — reflects any Origin *and* allows cookies | Explicit origin list with `credentials: true`; wildcards are forbidden by the Fetch spec with credentials | NestJS CORS — https://docs.nestjs.com/security/cors ; expressjs/cors — https://github.com/expressjs/cors#configuration-options | yes |
| 7 | `NestFactory.create(AppModule, { cors: true })` shipped to production unchanged (default = allow all origins) | Pass a CORS config object with an explicit `origin` allow-list | NestJS CORS — https://docs.nestjs.com/security/cors | yes |
| 8 | Origin allow-list built by `origin.endsWith('example.com')` — matches `evil-example.com` / `example.com.attacker.tld` | Exact-match origins (compare against a fixed set), not suffix/substring checks | expressjs/cors — https://github.com/expressjs/cors#configuration-options ; OWASP HTML5 CS — https://cheatsheetseries.owasp.org/cheatsheets/HTML5_Security_Cheat_Sheet.html | partial |
| 9 | Controller/route handler with no `@UseGuards(...)` and no global guard — route is open to the internet | Apply `@UseGuards(AuthGuard, RolesGuard)` per controller/handler, or register `APP_GUARD` globally | eslint-plugin-nestjs-security `require-guards` (CWE-284) — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; NestJS Guards — https://docs.nestjs.com/guards | partial |
| 10 | `@UseGuards` present but the guard `canActivate()` returns `true` unconditionally (stub/always-allow) | Guard must actually verify identity/role; never `return true` unconditionally | eslint-plugin-nestjs-security (decorator-presence caveat) — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | partial |
| 11 | `@Body() dto` consumed with no `ValidationPipe` bound (global, controller, or param) | Bind `ValidationPipe` (globally via `app.useGlobalPipes` / `APP_PIPE`, or `@UsePipes`) | eslint-plugin-nestjs-security `no-missing-validation-pipe` (CWE-20) — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; NestJS Pipes — https://docs.nestjs.com/pipes | partial |
| 12 | DTO properties declared with a TypeScript type but no `class-validator` decorators (`@IsEmail`, `@IsString`, `@IsInt`) — types vanish at runtime | Decorate every DTO field with a `class-validator` constraint | eslint-plugin-nestjs-security `require-class-validator` (CWE-20) — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; NestJS Validation — https://docs.nestjs.com/techniques/validation | yes |
| 13 | `ValidationPipe` without `whitelist: true` — extra properties in the body pass through to the service/entity | `new ValidationPipe({ whitelist: true })` strips undecorated properties | NestJS Validation — https://docs.nestjs.com/techniques/validation ; OWASP Mass Assignment CS — https://cheatsheetseries.owasp.org/cheatsheets/Mass_Assignment_Cheat_Sheet.html | yes |
| 14 | `ValidationPipe` without `forbidNonWhitelisted: true` — unexpected fields silently dropped, hiding privilege-escalation attempts in logs | `new ValidationPipe({ whitelist: true, forbidNonWhitelisted: true })` returns 400 on extra fields | eslint-plugin-nestjs-security — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; NestJS Validation — https://docs.nestjs.com/techniques/validation | yes |
| 15 | DTO carries a privilege field (`role: string`, `isAdmin: boolean`) bindable from the request body → mass assignment / privilege escalation | Remove privilege fields from the input DTO, or constrain with `@IsIn(['user'])`; server sets role | OWASP Mass Assignment CS — https://cheatsheetseries.owasp.org/cheatsheets/Mass_Assignment_Cheat_Sheet.html ; eslint-plugin-nestjs-security — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | partial |
| 16 | Binding the HTTP body directly to a TypeORM/Prisma entity/model instead of a DTO | Use a dedicated input DTO with only editable fields (allow-listing) | OWASP Mass Assignment CS — https://cheatsheetseries.owasp.org/cheatsheets/Mass_Assignment_Cheat_Sheet.html | partial |
| 17 | `ValidationPipe({ transform: true })` on a DTO with unconstrained fields lets attacker-controlled types be coerced onto the model | Combine `transform` with `whitelist`+`forbidNonWhitelisted` and per-field decorators | NestJS Validation — https://docs.nestjs.com/techniques/validation | partial |
| 18 | Returning the raw TypeORM/Prisma entity from a handler — leaks `passwordHash`, `secret`, tokens to the client | Map to a response DTO, or use `ClassSerializerInterceptor` with `@Exclude()` on sensitive fields | eslint-plugin-nestjs-security `no-exposed-private-fields` (CWE-200) — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; NestJS Serialization — https://docs.nestjs.com/techniques/serialization | partial |
| 19 | Sensitive entity column (`password`, `token`, `apiKey`, `privateKey`, `ssn`) with no `@Exclude()` and no response-DTO mapping | `@Exclude()` the column and apply `ClassSerializerInterceptor`, or never select it | eslint-plugin-nestjs-security `no-exposed-private-fields` — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; OWASP Node.js CS (return only what is necessary) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | yes |
| 20 | No `@nestjs/throttler` `ThrottlerModule` registered / no `ThrottlerGuard` — brute-force & DoS unbounded | `ThrottlerModule.forRoot([{ ttl, limit }])` + `APP_GUARD: ThrottlerGuard` | eslint-plugin-nestjs-security `require-throttler` (CWE-770) — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; NestJS Rate limiting — https://docs.nestjs.com/security/rate-limiting | partial |
| 21 | Login / auth endpoint with no `@Throttle(...)` and no global throttler — password brute-forcing | `@Throttle({ default: { limit: 5, ttl: 60000 } })` on the login handler | eslint-plugin-nestjs-security `require-throttler` — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; OWASP Node.js CS (brute-forcing) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |
| 22 | Hard-coded JWT secret in source (`jwtConstants.secret = 'secret'` / literal in `JwtModule.register`) | Load the secret from env/secrets vault via `ConfigService`; never commit it | NestJS Authentication (warning) — https://docs.nestjs.com/security/authentication ; OWASP Secrets Management CS — https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html | yes |
| 23 | `JwtModule.register({ secret, signOptions })` with no `expiresIn` — tokens never expire | Set `signOptions: { expiresIn: '15m' }` (short-lived) plus refresh tokens | NestJS Authentication — https://docs.nestjs.com/security/authentication ; OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | yes |
| 24 | `jwtService.verifyAsync(token, { ignoreExpiration: true })` — accepts expired tokens | Never set `ignoreExpiration: true`; let `verifyAsync` enforce `exp` | NestJS Authentication — https://docs.nestjs.com/security/authentication ; OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | yes |
| 25 | JWT verification without pinning `algorithms` — enables `alg:none` / RS→HS algorithm-confusion attacks | Pass `verifyOptions: { algorithms: ['RS256'] }` (or your single expected alg) | OWASP JWT CS (algorithm confusion) — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | yes |
| 26 | `jwt.decode()` used to trust claims (decode ≠ verify — no signature check) | Always `verify`/`verifyAsync`; `decode` only for non-security display | OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | yes |
| 27 | Symmetric JWT secret that is short/low-entropy (`'secret'`, `'changeme'`) | Use a long, high-entropy key (≥256-bit for HS256) or asymmetric keys | OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html ; NestJS Authentication — https://docs.nestjs.com/security/authentication | partial |
| 28 | Sensitive data (password hash, PII) placed in JWT payload (base64, not encrypted) | Keep payload minimal (`sub`, roles); never put secrets in a JWT | OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | partial |
| 29 | Storing passwords in plain text (comparing `user.password !== pass`) | Hash with bcrypt/argon2 and compare hashes | NestJS Authentication (warning) — https://docs.nestjs.com/security/authentication ; OWASP Password Storage CS — https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html | partial |
| 30 | No CSRF protection for cookie/session-authenticated, state-changing routes | `app.enableCsrfProtection()` (NestJS ≥ v12.1) or a double-submit token package | NestJS CSRF — https://docs.nestjs.com/security/csrf ; OWASP CSRF CS — https://cheatsheetseries.owasp.org/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.html | partial |
| 31 | Using the deprecated/vulnerable `csurf` package for CSRF | Use built-in `enableCsrfProtection()`, `csrf-csrf` (Express) or `@fastify/csrf-protection` (Fastify) | OWASP Node.js CS (csurf deprecated) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html ; NestJS CSRF — https://docs.nestjs.com/security/csrf | yes |
| 32 | State-changing logic behind a `@Get()` handler — bypasses CSRF (GET/HEAD/OPTIONS never checked) | Use `@Post/@Put/@Patch/@Delete` for state changes; GET must be side-effect-free | NestJS CSRF (limitations) — https://docs.nestjs.com/security/csrf ; OWASP CSRF CS — https://cheatsheetseries.owasp.org/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.html | partial |
| 33 | Over-broad CSRF `exclude` predicate (e.g. `request.url.startsWith('/public/')`) that a proxy can normalize into a protected path | Prefer an explicit list of `{ path, method }` exclusions over predicates | NestJS CSRF (predicate warning) — https://docs.nestjs.com/security/csrf | partial |
| 34 | Prisma `$queryRawUnsafe` / `$executeRawUnsafe` with user input concatenated into the SQL string | Use `$queryRaw`/`$executeRaw` tagged templates or parameterized `$1` markers | Prisma Raw queries — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries ; OWASP SQLi CS — https://cheatsheetseries.owasp.org/cheatsheets/SQL_Injection_Prevention_Cheat_Sheet.html | yes |
| 35 | `Prisma.raw(userInput)` or a faked tagged-template array to inject into `$queryRaw` | Use `Prisma.sql` with `${var}` interpolation; never `Prisma.raw` on untrusted input | Prisma Raw queries (unsafe use) — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries | yes |
| 36 | String-concatenated SQL in `$queryRaw` template (`` `... ${input}` `` built with `+`) | Interpolate variables as template placeholders so Prisma emits prepared statements | Prisma Raw queries — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries | partial |
| 37 | TypeORM `.query('SELECT ... ' + userInput)` — raw string SQL with concatenation | Use `manager.query(sql, [params])` parameter array, or the query builder | TypeORM Query Builder — https://typeorm.io/select-query-builder ; OWASP Query Parameterization CS — https://cheatsheetseries.owasp.org/cheatsheets/Query_Parameterization_Cheat_Sheet.html | yes |
| 38 | TypeORM `.where("name = '" + name + "'")` — concatenated predicate in query builder | `.where('name = :name', { name })` parameterized bindings | TypeORM Query Builder — https://typeorm.io/select-query-builder ; OWASP Query Parameterization CS — https://cheatsheetseries.owasp.org/cheatsheets/Query_Parameterization_Cheat_Sheet.html | yes |
| 39 | User input used as an `orderBy` / column / table identifier (parameters cannot bind identifiers) | Allow-list identifiers against a fixed set before interpolating | Prisma Raw queries (identifiers can't be parameterized) — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries ; OWASP SQLi CS — https://cheatsheetseries.owasp.org/cheatsheets/SQL_Injection_Prevention_Cheat_Sheet.html | partial |
| 40 | Global exception filter / handler that returns `error.stack` or the raw error to the client | Return a generic message + status; log details server-side only | OWASP Error Handling CS — https://cheatsheetseries.owasp.org/cheatsheets/Error_Handling_Cheat_Sheet.html ; NestJS Exception filters — https://docs.nestjs.com/exception-filters | yes |
| 41 | Running with `NODE_ENV` not set to `production`, exposing verbose stack traces & GraphQL IDE | Set `NODE_ENV=production`; disable dev-only introspection/IDE | OWASP Node.js CS (uncaughtException / info leakage) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html ; NestJS Helmet (GraphiQL note) — https://docs.nestjs.com/security/helmet | partial |
| 42 | Debug/health/introspection route reachable in production leaking internals | Gate debug endpoints behind env checks/guards; remove in prod | eslint-plugin-nestjs-security `no-exposed-debug-endpoints` (CWE-489) — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | partial |
| 43 | Secrets committed in `.env` that is tracked by git, or defaults baked into `ConfigModule` | Keep `.env` out of VCS; load secrets from a vault; validate presence at boot | OWASP Secrets Management CS — https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html ; NestJS Configuration — https://docs.nestjs.com/techniques/configuration | partial |
| 44 | `ConfigModule.forRoot()` without a `validationSchema` — app boots with missing/blank secrets | Provide a Joi/`class-validator` `validationSchema` so missing config fails at startup | NestJS Configuration (schema validation) — https://docs.nestjs.com/techniques/configuration | yes |
| 45 | No request body size limit — large payloads exhaust memory (DoS) | Set body size limits (`app.use(express.json({ limit: '1mb' }))` / Fastify `bodyLimit`) | OWASP Node.js CS (request size limits) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |
| 46 | Session/auth cookies set without `httpOnly`, `secure`, `sameSite` | Set `{ httpOnly: true, secure: true, sameSite: 'lax'|'strict' }` on cookies | OWASP Node.js CS (cookie flags) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | yes |
| 47 | `eval()`, `Function(...)`, or `child_process.exec` with request-derived input | Avoid `eval`/`exec`; use `execFile` with an argument array and validated input | OWASP Node.js CS (dangerous functions) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | yes |
| 48 | HTTP Parameter Pollution: array-typed query params trusted as scalars (`?foo=a&foo=b` → array) | Validate/normalize query shapes; use `hpp` or explicit typing | OWASP Node.js CS (HPP / input validation) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |
| 49 | Auto-generated / unused REST routes left enabled, widening attack surface | Remove/disable unused routes | OWASP Node.js CS (remove unnecessary routes) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | no |
| 50 | Out-of-date dependencies with known CVEs (`@nestjs/*`, `class-validator`, etc.) | Run `npm audit` / SCA in CI; pin and update dependencies | OWASP Node.js CS (keep packages up-to-date) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html ; OWASP Top 10 A06:2021 — https://owasp.org/Top10/A06_2021-Vulnerable_and_Outdated_Components/ | no |
| 51 | Object-level authorization missing (BOLA/IDOR) — handler trusts an `:id` param without ownership check | Verify the authenticated user owns/may access the requested object | OWASP API Security Top 10 2023 API1 (BOLA) — https://owasp.org/API-Security/editions/2023/en/0x11-t10/ | no |
| 52 | `@Res()` (Express response) used raw, bypassing Nest's serialization/interceptors (skips `@Exclude()`) | Return values (let Nest serialize) or use `@Res({ passthrough: true })` | NestJS Serialization — https://docs.nestjs.com/techniques/serialization | partial |
| 53 | Global `ValidationPipe` not registered at all (relying on ad-hoc per-DTO checks) | Register `ValidationPipe` globally with `whitelist`+`forbidNonWhitelisted` | NestJS Validation — https://docs.nestjs.com/techniques/validation | yes |
| 54 | ReDoS: user input matched against a catastrophic-backtracking regex (also in custom `@Matches()`) | Avoid evil regexes; validate with linear-time patterns / bounded input | OWASP Node.js CS (evil regexes) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |

---

## 2. Best practices

| # | best practice | antipattern it replaces | source (name + URL) | statically detectable |
|---|---------------|-------------------------|---------------------|-----------------------|
| 1 | Call `app.useSecurityHeaders()` (or `app.use(helmet())`) in `main.ts` before any other middleware | No security headers on responses | NestJS Helmet — https://docs.nestjs.com/security/helmet | yes |
| 2 | Keep the default restrictive CSP (`default-src 'self'; object-src 'none'; ...`); no `'unsafe-inline'` in `script-src` in prod | Loose/absent CSP | NestJS Helmet (CSP) — https://docs.nestjs.com/security/helmet ; OWASP CSP CS — https://cheatsheetseries.owasp.org/cheatsheets/Content_Security_Policy_Cheat_Sheet.html | yes |
| 3 | Keep HSTS enabled (`strictTransportSecurity` default `max-age=31536000; includeSubDomains`) | No HSTS → downgrade attacks | NestJS Helmet (default headers) — https://docs.nestjs.com/security/helmet ; OWASP HSTS CS — https://cheatsheetseries.owasp.org/cheatsheets/HTTP_Strict_Transport_Security_Cheat_Sheet.html | yes |
| 4 | Keep `X-Content-Type-Options: nosniff` and `X-Frame-Options: SAMEORIGIN`/`DENY` | MIME sniffing / clickjacking | NestJS Helmet — https://docs.nestjs.com/security/helmet | yes |
| 5 | Remove `X-Powered-By` (default with `useSecurityHeaders()`) | Framework fingerprinting | NestJS Helmet — https://docs.nestjs.com/security/helmet | yes |
| 6 | Configure CORS with an explicit `origin` allow-list (array/exact-match callback) | `origin: '*'` / `origin: true` | NestJS CORS — https://docs.nestjs.com/security/cors ; expressjs/cors — https://github.com/expressjs/cors#configuration-options | yes |
| 7 | Only combine `credentials: true` with a concrete origin list, never a wildcard | Credentials + wildcard (spec-forbidden) | NestJS CORS — https://docs.nestjs.com/security/cors ; expressjs/cors — https://github.com/expressjs/cors#configuration-options | yes |
| 8 | On Fastify, set `methods` explicitly (defaults to CORS-safelisted `GET,HEAD,POST` only) | Cross-origin PUT/PATCH/DELETE rejected/misconfigured | NestJS CORS (default methods) — https://docs.nestjs.com/security/cors | yes |
| 9 | Apply `@UseGuards(AuthGuard, RolesGuard)` per controller/handler, or register `APP_GUARD` globally and mark exceptions `@Public()` | Unprotected route | eslint-plugin-nestjs-security `require-guards` — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic ; NestJS Authentication (global guard) — https://docs.nestjs.com/security/authentication | partial |
| 10 | Guards must implement real authorization (`canActivate` checks JWT + role), never `return true` | Stub/always-allow guard | NestJS Guards — https://docs.nestjs.com/guards | partial |
| 11 | Register `ValidationPipe` globally (`app.useGlobalPipes(new ValidationPipe(...))` or `APP_PIPE`) | `@Body()` consumed unvalidated | NestJS Validation — https://docs.nestjs.com/techniques/validation | yes |
| 12 | `new ValidationPipe({ whitelist: true, forbidNonWhitelisted: true, transform: true })` | Extra/unexpected properties reaching the model | NestJS Validation — https://docs.nestjs.com/techniques/validation ; eslint-plugin-nestjs-security — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | yes |
| 13 | Decorate every DTO field with `class-validator` constraints (`@IsEmail`, `@IsInt`, `@IsIn`, `@Length`) | Bare typed DTO fields | NestJS Validation — https://docs.nestjs.com/techniques/validation ; eslint-plugin-nestjs-security `require-class-validator` — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | yes |
| 14 | Use input DTOs that contain only user-editable fields (allow-listing); exclude `role`/`isAdmin` | Mass assignment / privilege escalation | OWASP Mass Assignment CS — https://cheatsheetseries.owasp.org/cheatsheets/Mass_Assignment_Cheat_Sheet.html | partial |
| 15 | Never bind the HTTP body directly to a persistence entity/model | Body → entity autobinding | OWASP Mass Assignment CS — https://cheatsheetseries.owasp.org/cheatsheets/Mass_Assignment_Cheat_Sheet.html | partial |
| 16 | Map outputs to response DTOs, or use `ClassSerializerInterceptor` + `@Exclude()` on secrets | Returning raw entity with secrets | NestJS Serialization — https://docs.nestjs.com/techniques/serialization ; eslint-plugin-nestjs-security `no-exposed-private-fields` — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | partial |
| 17 | Return only necessary fields (project/select the columns a response needs) | Over-fetching PII/secrets | OWASP Node.js CS (only return what is necessary) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |
| 18 | Register `ThrottlerModule.forRoot([{ ttl, limit }])` + global `APP_GUARD: ThrottlerGuard` | No rate limiting | NestJS Rate limiting — https://docs.nestjs.com/security/rate-limiting ; eslint-plugin-nestjs-security `require-throttler` — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | partial |
| 19 | Add a stricter `@Throttle({ default: { limit: 5, ttl: 60000 } })` on login/auth handlers | Unthrottled login (brute force) | NestJS Rate limiting — https://docs.nestjs.com/security/rate-limiting ; OWASP Node.js CS (brute forcing) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |
| 20 | Load the JWT secret from `ConfigService`/env/secrets vault, never a literal in source | Hard-coded JWT secret | NestJS Authentication (warning) — https://docs.nestjs.com/security/authentication ; OWASP Secrets Management CS — https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html | yes |
| 21 | Set a short `signOptions.expiresIn` (e.g. `'15m'`) and use refresh tokens | Non-expiring tokens | NestJS Authentication — https://docs.nestjs.com/security/authentication ; OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | yes |
| 22 | Let `verifyAsync` enforce `exp`; never pass `ignoreExpiration: true` | Accepting expired tokens | NestJS Authentication — https://docs.nestjs.com/security/authentication | yes |
| 23 | Pin `algorithms` on verify (`{ algorithms: ['RS256'] }`) to block `alg:none`/confusion | Unpinned algorithm | OWASP JWT CS (algorithm confusion) — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | yes |
| 24 | Always `verify()` tokens (signature); use `decode()` only for non-trusted display | Trusting `decode()` output | OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | yes |
| 25 | Use a high-entropy HS256 key (≥256-bit) or asymmetric RS256/ES256 key pair | Weak/short symmetric secret | OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | partial |
| 26 | Keep JWT payload minimal (`sub`, `username`, roles); no secrets/PII | Sensitive data in JWT | OWASP JWT CS — https://cheatsheetseries.owasp.org/cheatsheets/JSON_Web_Token_for_Java_Cheat_Sheet.html | partial |
| 27 | Hash passwords with bcrypt/argon2; compare hashes, never plaintext | Plaintext password compare | NestJS Authentication (warning) — https://docs.nestjs.com/security/authentication ; OWASP Password Storage CS — https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html | partial |
| 28 | Enable `app.enableCsrfProtection()` for cookie/session-authenticated apps | No CSRF defense | NestJS CSRF — https://docs.nestjs.com/security/csrf ; OWASP CSRF CS — https://cheatsheetseries.owasp.org/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.html | partial |
| 29 | Use `csrf-csrf` (Express) or `@fastify/csrf-protection` (Fastify) for token schemes; not `csurf` | Deprecated `csurf` | NestJS CSRF — https://docs.nestjs.com/security/csrf ; OWASP Node.js CS — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | yes |
| 30 | Keep GET/HEAD/OPTIONS side-effect-free; use POST/PUT/PATCH/DELETE for state changes | State change on GET | NestJS CSRF (limitations) — https://docs.nestjs.com/security/csrf | partial |
| 31 | List cross-origin frontends in CSRF `trustedOrigins` (CORS allow-list is not trusted automatically) | Cross-origin credentialed requests blocked or over-allowed | NestJS CSRF (trusted origins warning) — https://docs.nestjs.com/security/csrf | yes |
| 32 | Prefer explicit `{ path, method }` CSRF exclusions over predicate functions | Over-broad predicate exclusion | NestJS CSRF — https://docs.nestjs.com/security/csrf | partial |
| 33 | Use Prisma `$queryRaw`/`$executeRaw` tagged templates (auto-parameterized prepared statements) | `$queryRawUnsafe` with concatenation | Prisma Raw queries — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries | partial |
| 34 | If a raw string is unavoidable, use `$queryRawUnsafe(sql, $1, $2)` parameterized markers | Concatenated `$queryRawUnsafe` | Prisma Raw queries (parameterized) — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries | yes |
| 35 | Build separated raw queries with the `Prisma.sql` / `Prisma.join` helpers, never `Prisma.raw` on input | `Prisma.raw(userInput)` | Prisma Raw queries — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries | yes |
| 36 | TypeORM: bind parameters (`.where('x = :v', { v })`, `manager.query(sql, [params])`) | Concatenated SQL / predicates | TypeORM Query Builder — https://typeorm.io/select-query-builder ; OWASP Query Parameterization CS — https://cheatsheetseries.owasp.org/cheatsheets/Query_Parameterization_Cheat_Sheet.html | yes |
| 37 | Allow-list identifiers (columns/tables/`orderBy`) against a fixed set before interpolation | User input as SQL identifier | Prisma Raw queries — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries ; OWASP SQLi CS — https://cheatsheetseries.owasp.org/cheatsheets/SQL_Injection_Prevention_Cheat_Sheet.html | partial |
| 38 | Prefer ORM model methods (`findMany`, query builder) over raw SQL wherever possible | Unnecessary raw SQL | Prisma Raw queries — https://www.prisma.io/docs/orm/prisma-client/using-raw-sql/raw-queries | partial |
| 39 | Custom exception filter returns a generic message + status; logs stack server-side only | Stack trace to client | NestJS Exception filters — https://docs.nestjs.com/exception-filters ; OWASP Error Handling CS — https://cheatsheetseries.owasp.org/cheatsheets/Error_Handling_Cheat_Sheet.html | yes |
| 40 | Run with `NODE_ENV=production`; disable GraphQL IDE/introspection and dev error output | Verbose prod errors / dev IDE | OWASP Node.js CS — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html ; NestJS Helmet (GraphiQL) — https://docs.nestjs.com/security/helmet | partial |
| 41 | Gate/remove debug & introspection endpoints in production | Debug endpoint in prod | eslint-plugin-nestjs-security `no-exposed-debug-endpoints` — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | partial |
| 42 | Provide a `validationSchema` (Joi/`class-validator`) in `ConfigModule.forRoot` so missing config fails at boot | Silent missing/blank config | NestJS Configuration — https://docs.nestjs.com/techniques/configuration | yes |
| 43 | Keep `.env` out of version control; load secrets from a vault/secret manager | Committed secrets | OWASP Secrets Management CS — https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html | partial |
| 44 | Set body size limits (`express.json({ limit })` / Fastify `bodyLimit`) | Unbounded request body | OWASP Node.js CS (request size limits) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |
| 45 | Set `httpOnly`, `secure`, `sameSite` on session/auth cookies | Insecure cookie flags | OWASP Node.js CS (cookie flags) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | yes |
| 46 | Avoid `eval`/`Function`/`child_process.exec`; use `execFile` with argument arrays | Dangerous dynamic execution | OWASP Node.js CS (dangerous functions) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | yes |
| 47 | Normalize/validate query parameter shapes (guard against HPP array/object injection) | HTTP Parameter Pollution | OWASP Node.js CS (HPP) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |
| 48 | Enforce object-level authorization (ownership checks) on every `:id`-addressed resource | BOLA/IDOR | OWASP API Security Top 10 2023 API1 — https://owasp.org/API-Security/editions/2023/en/0x11-t10/ | no |
| 49 | Return values from handlers (let Nest serialize) or use `@Res({ passthrough: true })` | Raw `@Res()` bypassing serialization | NestJS Serialization — https://docs.nestjs.com/techniques/serialization | partial |
| 50 | Run `npm audit`/SCA in CI and keep `@nestjs/*` and validators patched | Vulnerable/outdated deps | OWASP Node.js CS (keep packages up-to-date) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html ; OWASP Top 10 A06:2021 — https://owasp.org/Top10/A06_2021-Vulnerable_and_Outdated_Components/ | no |
| 51 | Run security linters (eslint-plugin-nestjs-security, eslint-plugin-security) in CI/pre-commit | No automated decorator/security checks | OWASP Node.js CS (run security linters) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html ; eslint-plugin-nestjs-security — https://dev.to/ofri-peretz/getting-started-with-eslint-plugin-nestjs-security-32ic | n/a |
| 52 | Avoid catastrophic-backtracking regexes in `@Matches()` / custom validators | ReDoS | OWASP Node.js CS (evil regexes) — https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html | partial |

---

## Notes for keur rule development

**High-confidence static rules (yes).** These have a concrete, greppable API
surface and are the strongest candidates for `.rule` files:

- CORS misconfig: `enableCors(` / `cors:` with `origin: '*'`, `origin: true`,
  or `credentials: true` + wildcard (antipatterns 5–7).
- `ValidationPipe` missing `whitelist`/`forbidNonWhitelisted` (13–14).
- Hard-coded JWT secret literal in `JwtModule.register`/`registerAsync` (22).
- JWT `ignoreExpiration: true` / missing `expiresIn` / missing `algorithms` (23–25).
- `jwt.decode(` used where `verify` is expected (26).
- Prisma `$queryRawUnsafe` / `$executeRawUnsafe` / `Prisma.raw(` (34–35).
- TypeORM `.query(` with `+` concatenation / concatenated `.where(` (37–38).
- Deprecated `csurf` import (31).
- Cookie options missing `httpOnly`/`secure`/`sameSite` (46).
- `eval(` / `child_process.exec(` (47).
- `ConfigModule.forRoot(` without `validationSchema` (44).
- DTO fields without `class-validator` decorators (12) — needs an AST/decorator
  walk like eslint-plugin-nestjs-security; a regex can approximate.

**Presence/absence rules (partial).** keur's `presence`/`absence` engines fit
"is X registered anywhere in the app": missing `useSecurityHeaders()`/`helmet`
(1), missing `ThrottlerModule`/`ThrottlerGuard` (20), missing global
`ValidationPipe` (53), missing `enableCsrfProtection` (30). These match the
eslint plugin's `assumeGlobal*` caveat: a controller-level absence is a false
positive if the control is registered globally, so pair a per-handler check with
an app-level presence check.

**Not statically detectable (no).** Object-level authorization/BOLA (51),
correctness of a guard's authorization logic (10), unused-route pruning (49),
and dependency CVEs (50) need runtime, data-flow, or SCA — out of scope for a
regex rule engine, better handled by pipeline-coverage facts.

**Provenance.** The six eslint-plugin-nestjs-security rules
(`require-guards` CWE-284, `no-exposed-debug-endpoints` CWE-489,
`no-missing-validation-pipe` CWE-20, `require-class-validator` CWE-20,
`no-exposed-private-fields` CWE-200, `require-throttler` CWE-770) are a directly
reusable taxonomy — each already carries a CWE and an OWASP Top 10 mapping.
