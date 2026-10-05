# Nx Monorepo Security & Configuration Hardening — Antipatterns and Best Practices

Research feed for keur rule development. Scope: `nx.json` / `project.json`
misconfiguration, task-caching poisoning, module boundary enforcement
(`@nx/enforce-module-boundaries`), dependency constraints, Nx Cloud access
tokens in the repo, untrusted plugins, target defaults, and CI security.

All entries are grounded in Nx's own docs (nx.dev), the Nx GitHub Security
Advisory / postmortem for the August 2025 "s1ngularity" supply-chain attack, and
OWASP guidance. Nothing here is invented; where the topic did not support 50
distinct authoritative items, the list stops rather than padding.

"Statically detectable" is judged from the perspective of keur: a deterministic
scanner over committed files (`nx.json`, `project.json`, `package.json`,
`.eslintrc*`, `.github/workflows/*.yml`, `nx-cloud.env`, `.gitignore`). "yes" =
a rule can decide from file contents alone. "partial" = a rule can flag a strong
signal but needs context/config resolution to confirm. "no" = only observable at
runtime, in the Nx Cloud UI, or in CI provider settings outside the repo.

Key source URLs (short names used in the tables):

- **Cache Security** — <https://nx.dev/docs/concepts/ci-concepts/cache-security>
- **Access Tokens** — <https://nx.dev/docs/guides/nx-cloud/access-tokens>
- **Cloud Runner Config** — <https://nx.dev/docs/reference/nx-cloud/config>
- **Personal Access Tokens** — <https://nx.dev/docs/guides/nx-cloud/personal-access-tokens>
- **Enforce Module Boundaries (rule)** — <https://nx.dev/docs/kb/enforce-module-boundaries>
- **Enforce Module Boundaries (feature)** — <https://nx.dev/docs/features/enforce-module-boundaries>
- **Ban External Imports** — <https://nx.dev/docs/guides/enforce-module-boundaries/ban-external-imports>
- **Inputs & Named Inputs** — <https://nx.dev/docs/reference/inputs>
- **Fix Sandbox Violations** — <https://nx.dev/docs/guides/nx-cloud/fix-sandbox-violations>
- **Sandboxing** — <https://nx.dev/docs/features/ci-features/sandboxing>
- **Encryption** — <https://nx.dev/docs/guides/nx-cloud/encryption>
- **s1ngularity postmortem** — <https://nx.dev/blog/s1ngularity-postmortem>
- **GHSA-cxm3-wv7p-598c** — <https://github.com/nrwl/nx/security/advisories/GHSA-cxm3-wv7p-598c>
- **NPM Trusted Publishers** — <https://docs.npmjs.com/trusted-publishers/>
- **GH Actions pull_request_target** — <https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#pull_request_target>
- **OWASP CI/CD Top 10** — <https://owasp.org/www-project-top-10-ci-cd-security-risks/>
- **OWASP Secrets Management CS** — <https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html>
- **OWASP Vuln Dependency Mgmt CS** — <https://cheatsheetseries.owasp.org/cheatsheets/Vulnerable_Dependency_Management_Cheat_Sheet.html>

---

## 1. Nx Cloud access tokens & secrets

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|-------------|-----------------------------|----------------------|-----------------------|
| 1 | Committing a CI access token via `nxCloudAccessToken` in `nx.json` — visible to anyone with repo access; a read-write token grants full cache write and enables cache poisoning | Never put `nxCloudAccessToken` in `nx.json`; supply the token via the `NX_CLOUD_ACCESS_TOKEN` env var / CI secret only | Cache Security ("Avoid using CI access tokens in nx.json") <https://nx.dev/docs/concepts/ci-concepts/cache-security> | yes — key `nxCloudAccessToken` present in `nx.json` |
| 2 | Legacy `tasksRunnerOptions.default.options.accessToken` with a literal token committed in `nx.json` | Migrate to `nxCloudId` + env-var token; do not embed `accessToken` in config | Access Tokens (Legacy methods) <https://nx.dev/docs/guides/nx-cloud/access-tokens> | yes — `accessToken` string under `tasksRunnerOptions` |
| 3 | Committing `nxCloudEncryptionKey` (the e2e encryption secret) in `nx.json`, defeating the point of e2e encryption | Provide the key via `NX_CLOUD_ENCRYPTION_KEY` env var, not committed config | Cloud Runner Config (encryption) <https://nx.dev/docs/reference/nx-cloud/config> | yes — key `nxCloudEncryptionKey` in `nx.json` |
| 4 | Using a read-write CI token on unprotected / PR branches, allowing any contributor to poison the shared global cache | Use a `read-write` token only on protected branches; use a `read-only` token everywhere else (isolated per-branch cache) | Access Tokens (Access types) <https://nx.dev/docs/guides/nx-cloud/access-tokens> | partial — token scoping lives in CI provider settings; repo can only show whether env-conditioned |
| 5 | Committing an `nx-cloud.env` file (which stores `NX_CLOUD_ACCESS_TOKEN` / `NX_CLOUD_ENCRYPTION_KEY`) to the repo | Keep `nx-cloud.env` out of source control — add it to `.gitignore` | Cloud Runner Config ("often added to .gitignore") <https://nx.dev/docs/reference/nx-cloud/config> | yes — file `nx-cloud.env` tracked / not in `.gitignore` |
| 6 | Storing the workspace ID Access Level at default `read-write` for unauthenticated users, so anyone who clones can read/write the cache | Set workspace ID Access Level to `read-only` or `none` and require personal-access-token login | Cache Security (PATs) <https://nx.dev/docs/concepts/ci-concepts/cache-security> | no — Nx Cloud UI workspace setting, not in repo |
| 7 | Hard-coding the raw token in `.github/workflows/*.yml` (e.g. `NX_CLOUD_ACCESS_TOKEN: nxcg_...`) instead of referencing a secret | Reference the token as `${{ secrets.NX_CLOUD_ACCESS_TOKEN }}` (or provider equivalent) | Access Tokens (GitHub Actions example) <https://nx.dev/docs/guides/nx-cloud/access-tokens> ; OWASP Secrets Management CS <https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html> | yes — literal `nxcg_`/token pattern next to `NX_CLOUD_ACCESS_TOKEN` in a workflow |
| 8 | Sharing the local cache by pointing the `~/.nx/<id>/cache` folder at a network drive, voiding cache immutability and enabling direct poisoning | Never manually share the local cache; use Nx Cloud remote caching for sharing | Cache Security ("Do not manually share your local cache") <https://nx.dev/docs/concepts/ci-concepts/cache-security> | partial — a relocated `cacheDirectory` in `nx.json` pointing at a share is a signal |
| 9 | Disabling Nx Cloud timeouts blindly in CI (`NX_CLOUD_NO_TIMEOUTS=true`) as a permanent workaround, masking connectivity/security issues | Use timeouts as designed; only set `NX_CLOUD_NO_TIMEOUTS` for targeted debugging | Cloud Runner Config (Timeouts) <https://nx.dev/docs/reference/nx-cloud/config> | yes — `NX_CLOUD_NO_TIMEOUTS=true` in committed CI files/env |
| 10 | Committing personal access tokens (user-scoped) into the repo or shared config | PATs are provisioned per-developer via `nx login`; keep them local, never commit | Personal Access Tokens <https://nx.dev/docs/guides/nx-cloud/personal-access-tokens> | partial — token-shaped strings in tracked files |

---

## 2. Task caching poisoning & sandbox / input correctness

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|-------------|-----------------------------|----------------------|-----------------------|
| 11 | Trusting a shared remote cache written with a read-write token by untrusted actors — cached output files are replayed and can execute arbitrary code on dev machines / CI | Restrict write access (read-write only on protected branches); treat cache as an execution vector | Cache Security ("What data is sent to the cache") <https://nx.dev/docs/concepts/ci-concepts/cache-security> | no — runtime/trust-model property |
| 12 | Deploying build artifacts straight from the cache, so a poisoned cache entry reaches end users | Skip the cache (`--skip-nx-cache`) when creating the deployment artifact | Cache Security ("Skip the cache when creating a deployment artifact") <https://nx.dev/docs/concepts/ci-concepts/cache-security> | partial — presence/absence of `--skip-nx-cache` on deploy targets in CI files |
| 13 | Not enabling end-to-end encryption, so cache artifacts sit in plaintext on the cache server | Configure e2e encryption via `NX_CLOUD_ENCRYPTION_KEY` (env, not committed) | Encryption <https://nx.dev/docs/guides/nx-cloud/encryption> ; Cache Security | partial — absence is a config gap, key must not be committed |
| 14 | A task reads/writes files outside its declared `inputs`/`outputs` (sandbox violation), so the cache key is wrong and a stale/poisoned result is replayed | Fix the config: declare the file in `inputs`/`outputs`, add a `dependsOn`, or use `dependentTasksOutputFiles`; keep tasks hermetic | Fix Sandbox Violations <https://nx.dev/docs/guides/nx-cloud/fix-sandbox-violations> ; Sandboxing <https://nx.dev/docs/features/ci-features/sandboxing> | no — detected by Nx sandbox at runtime, reported in Nx Cloud dashboard |
| 15 | An app reads/writes a runtime-only file during its `build` task, corrupting cache correctness | Fix the offending code so the build is hermetic; the file should not be touched at build time | Fix Sandbox Violations ("Task or application bug") <https://nx.dev/docs/guides/nx-cloud/fix-sandbox-violations> | no — behavioral, runtime |
| 16 | Overusing `exclude-reads`/`exclude-writes`/`task-exclusions` in `.nx/workflows/sandboxing-config.yaml` to silence violations — excluded paths are no longer tracked, hiding real regressions | Use sandbox exclusions sparingly (like a `ts-ignore`), only for genuinely benign access | Fix Sandbox Violations ("Benign access, last resort") <https://nx.dev/docs/guides/nx-cloud/fix-sandbox-violations> | partial — count/breadth of exclusions in `.nx/workflows/sandboxing-config.yaml` |
| 17 | A task that depends on an env var (e.g. `API_KEY`) does not list it as an `{ "env": ... }` input, so cache is reused across differing secrets/configs | Declare environment-variable inputs: `{ "env": "API_KEY" }` for tasks whose behavior depends on it | Inputs (Environment variables) <https://nx.dev/docs/reference/inputs> | partial — compare referenced env vars vs declared `env` inputs |
| 18 | Narrowing `externalDependencies` inputs incorrectly (e.g. omitting a plugin), producing false cache hits when a dependency version changes | Let Nx consider all external deps by default, or list the complete set (`eslint`, `eslint-config-airbnb`, ...) when narrowing | Inputs (External dependencies) <https://nx.dev/docs/reference/inputs> | partial — narrowed `externalDependencies` list in config |
| 19 | Marking a non-deterministic target as `cache: true`, caching results that depend on unlisted runtime state | Only mark deterministic tasks cacheable; capture nondeterminism via `runtime`/`env` inputs | Cloud Runner Config (Cacheable operations) <https://nx.dev/docs/reference/nx-cloud/config> ; Inputs (Runtime inputs) <https://nx.dev/docs/reference/inputs> | partial — `cache: true` present but hard to prove determinism statically |
| 20 | Using an OS/shell-specific runtime input script (`.sh`/`.bat`) so hashes diverge across platforms and cache correctness breaks | Use cross-platform runtime inputs (e.g. `{ "runtime": "node --version" }`), never `.sh`/`.bat` | Inputs (Runtime inputs) <https://nx.dev/docs/reference/inputs> | yes — `.sh`/`.bat` inside a `"runtime"` input string |
| 21 | Ignoring the "Unknown Local Cache Error" warning (a cache entry created on a different machine) instead of treating it as possible poisoning | Investigate unknown-local-cache warnings; do not share/relocate the local cache | Cache Security <https://nx.dev/docs/concepts/ci-concepts/cache-security> | no — runtime warning |
| 22 | Over-broad `dependentTasksOutputFiles` glob causing accidental false cache hits when the set of dependency tasks changes | Scope the glob tightly (e.g. `**/*.d.ts`) so only intended dependency outputs feed the hash | Inputs (Outputs of dependent tasks) <https://nx.dev/docs/reference/inputs> | partial — an over-broad glob (`**/*`) in `dependentTasksOutputFiles` |
| 23 | Directory input without trailing slash/glob (`{projectRoot}/src`), silently matching no files so source changes don't invalidate the cache | Always use a trailing slash or glob for directory inputs (`{projectRoot}/src/` or `.../src/**/*`) | Inputs (Directory paths) <https://nx.dev/docs/reference/inputs> | yes — input string ends in a bare directory name with no `/` or `*` |

---

## 3. Module boundaries & dependency constraints (@nx/enforce-module-boundaries)

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|-------------|-----------------------------|----------------------|-----------------------|
| 24 | No `@nx/enforce-module-boundaries` rule configured, so any library can import any other — unplanned cross-dependencies and no architectural isolation | Add `@nx/enforce-module-boundaries` to ESLint config and set it to `"error"` | Enforce Module Boundaries (rule) <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — rule absent from `.eslintrc*` / `eslint.config.*` |
| 25 | The rule present but set to `"warn"` / `"off"`, so violations never block | Configure the rule at `"error"` severity | Enforce Module Boundaries (rule) <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — severity `"warn"`/`"off"` on `@nx/enforce-module-boundaries` |
| 26 | Empty `depConstraints: []` (rule enabled but no constraints), providing zero real enforcement | Define `depConstraints` with `sourceTag`/`onlyDependOnLibsWithTags` per architecture | Enforce Module Boundaries (rule) — depConstraints <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — `depConstraints` empty while rule enabled |
| 27 | Catch-all constraint `{ sourceTag: "*", onlyDependOnLibsWithTags: ["*"] }` that permits every dependency, nullifying boundaries | Use specific tags; avoid a wildcard `*` that allows everything | Enforce Module Boundaries (feature) <https://nx.dev/docs/features/enforce-module-boundaries> | yes — `"*"` in both source and allow lists |
| 28 | Projects untagged (no `tags` in `project.json`), so tag-based constraints match nothing and silently pass | Tag every project (scope/type dimensions) so constraints actually apply | Enforce Module Boundaries (feature) <https://nx.dev/docs/features/enforce-module-boundaries> | yes — `project.json` with no `tags` in a workspace that uses `depConstraints` |
| 29 | Frontend/backend isolation not enforced, letting a backend project import frontend packages (and vice-versa) | Ban cross-tier external imports with `bannedExternalImports` per source tag | Ban External Imports <https://nx.dev/docs/guides/enforce-module-boundaries/ban-external-imports> | yes — missing `bannedExternalImports` for tiered tags (partial: needs tag design) |
| 30 | Allowing arbitrary npm packages into a security-sensitive project instead of an allow-list | Use `allowedExternalImports` as an exclusive allow-list of permitted npm packages | Enforce Module Boundaries (rule) — depConstraints <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — presence/absence of `allowedExternalImports` on sensitive tags |
| 31 | Importing transitive dependencies not declared in `package.json` ("phantom dependencies"), which break when the transitive dep is removed and evade audit | Enable `banTransitiveDependencies: true` to forbid importing undeclared deps | Enforce Module Boundaries (rule) — options <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — `banTransitiveDependencies` not set to true |
| 32 | Not applying external-import constraints to nested/transitive external packages, so a banned package sneaks in via a dependency | Enable `checkNestedExternalImports: true` to apply banned/allowed lists transitively | Enforce Module Boundaries (rule) — options <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — option absent when nested checks are desired (partial) |
| 33 | Buildable libraries importing non-buildable libraries, producing broken/incomplete build artifacts | Enable `enforceBuildableLibDependency: true` | Enforce Module Boundaries (rule) — options <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — option present/absent |
| 34 | Over-broad `allow` list (imports allowed without any checks) that bypasses all boundary enforcement | Keep `allow` minimal; do not use it to whitelist away real violations | Enforce Module Boundaries (rule) — options <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — large / wildcard `allow` array |
| 35 | Silencing self-circular-dependency checks with `allowCircularSelfDependency: true` to avoid fixing a real cycle | Leave `allowCircularSelfDependency` at default `false`; fix the cycle | Enforce Module Boundaries (rule) — options <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — `allowCircularSelfDependency: true` |
| 36 | Broad `ignoredCircularDependencies` with `*` wildcards, hiding genuine circular dependencies | Keep `ignoredCircularDependencies` narrow and justified | Enforce Module Boundaries (rule) — options <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — `*` entries in `ignoredCircularDependencies` |
| 37 | Bypassing boundaries by importing via deep relative paths (`../../other-lib/src/...`) instead of the project's public entry point | Import only through the library's published entry point / path alias; the rule flags deep relative reach-ins | Taming Code Organization with Module Boundaries in Nx <https://nx.dev/blog/mastering-the-project-boundaries-in-nx> | partial — deep `../` import into another lib's `src` is a strong signal |
| 38 | Overusing `checkDynamicDependenciesExceptions` to permit importing lazy-loaded libraries eagerly | Keep the lazy-load exceptions list minimal | Enforce Module Boundaries (rule) — options <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — broad `checkDynamicDependenciesExceptions` |
| 39 | Disabling the rule inline with `// eslint-disable @nx/enforce-module-boundaries` to push a violating import | Fix the boundary; do not disable the rule inline | Enforce Module Boundaries (rule) <https://nx.dev/docs/kb/enforce-module-boundaries> | yes — inline eslint-disable of this specific rule |

---

## 4. nx.json / project.json misconfig & target defaults

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|-------------|-----------------------------|----------------------|-----------------------|
| 40 | `nx.json` present but `neverConnectToCloud` not set in an org that has a policy to never use Nx Cloud, leaving cloud prompts/connection possible | Set `"neverConnectToCloud": true` (and/or `NX_NO_CLOUD=true`) when policy forbids cloud | Cloud Runner Config (Disabling connections) <https://nx.dev/docs/reference/nx-cloud/config> | yes — key present/absent (org-policy dependent) |
| 41 | `targetDefaults` marking broad targets `cache: true` without correct `inputs`, so unrelated changes don't bust the cache | Pair every cacheable `targetDefault` with correct `inputs`/`namedInputs` | Cloud Runner Config (Cacheable operations) <https://nx.dev/docs/reference/nx-cloud/config> ; Inputs <https://nx.dev/docs/reference/inputs> | partial — `cache: true` in `targetDefaults` without matching inputs |
| 42 | `production` named input not excluding dev/test config, so production builds hash (and re-run on) irrelevant tooling files | Define `production` as `default` minus non-production files (e.g. `!{projectRoot}/**/*.spec.ts`, `!.../jest.config.ts`) | Inputs (Production inputs) <https://nx.dev/docs/reference/inputs> | yes — `production` named input equals `default` with no exclusions |
| 43 | Relying on gitignored files as task inputs — Nx excludes gitignored files from hashing, so their changes never invalidate cache | Do not depend on gitignored files for correctness; make required inputs tracked | Inputs ("Gitignored Files Are Excluded") <https://nx.dev/docs/reference/inputs> | partial — input glob overlapping `.gitignore` entries |
| 44 | `.nxignore` / ignore files accidentally excluding `project.json`/`package.json`, so projects silently disappear from the graph (and from boundary/lint enforcement) | Never ignore `project.json`/`package.json`; verify the project graph after ignore-file edits | Nx GitHub issue #76970713 (project detection) <https://stackoverflow.com/questions/76970713> | yes — `project.json`/`package.json`/`*.json` patterns in `.nxignore`/`.gitignore` |
| 45 | Committing secrets/API keys into `nx.json`/`project.json` (any field), which then leak via repo access and end up in the cache hash surface | Keep secrets in env/secret stores; never in Nx config files | OWASP Secrets Management CS <https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html> ; s1ngularity postmortem (avoid plaintext tokens in config) <https://nx.dev/blog/s1ngularity-postmortem> | yes — secret-shaped strings in `nx.json`/`project.json` |
| 46 | Absolute `workingDirectory: "absolute"` input assumptions that leak local filesystem paths into cache keys and break CI/local parity | Prefer `workingDirectory: "relative"` unless absolute location genuinely matters | Inputs (Working directory) <https://nx.dev/docs/reference/inputs> | yes — `"workingDirectory": "absolute"` in inputs |

---

## 5. Untrusted plugins & supply-chain (s1ngularity lessons)

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|-------------|-----------------------------|----------------------|-----------------------|
| 47 | Installing/running an untrusted Nx plugin that executes `postinstall` scripts (the s1ngularity payload ran in a postinstall hook harvesting tokens/SSH keys) | Vet plugins; prefer first-party `@nx/*`; consider `--ignore-scripts` / disabling install scripts for untrusted deps; pin exact versions | s1ngularity postmortem <https://nx.dev/blog/s1ngularity-postmortem> ; GHSA-cxm3-wv7p-598c <https://github.com/nrwl/nx/security/advisories/GHSA-cxm3-wv7p-598c> | partial — third-party plugin with lifecycle scripts in `package.json` |
| 48 | Floating/`latest` version ranges for `nx` and `@nx/*` plugins, so a malicious republished version is pulled automatically | Pin exact versions and use a committed lockfile; review Nx updates before applying | OWASP Vuln Dependency Mgmt CS <https://cheatsheetseries.owasp.org/cheatsheets/Vulnerable_Dependency_Management_Cheat_Sheet.html> ; s1ngularity postmortem <https://nx.dev/blog/s1ngularity-postmortem> | yes — `^`/`~`/`latest`/`*` ranges on `nx`/`@nx/*` in `package.json` |
| 49 | Having any of the known-malicious versions installed (`nx` 20.9.0/20.10.0/20.12.0/21.5.0/21.6.0; `@nx/devkit`/`js`/`workspace`/`node` 21.5.0/20.9.0; `@nx/eslint` 21.5.0; `@nx/key`/`@nx/enterprise-cloud` 3.2.0) | Remove/upgrade off the compromised versions; rotate any exposed credentials | s1ngularity postmortem (Affected versions) <https://nx.dev/blog/s1ngularity-postmortem> ; GHSA-cxm3-wv7p-598c <https://github.com/nrwl/nx/security/advisories/GHSA-cxm3-wv7p-598c> | yes — exact bad versions pinned in `package.json`/lockfile |
| 50 | Trusting an installed Nx package without provenance (the malicious builds lacked NPM provenance signing) | Enable/verify NPM provenance; Nx now adds provenance checks — prefer provenance-signed packages | s1ngularity postmortem <https://nx.dev/blog/s1ngularity-postmortem> ; NPM Trusted Publishers <https://docs.npmjs.com/trusted-publishers/> | no — provenance verified at install/registry time |
| 51 | Ignoring post-compromise persistence: malicious code appended `sudo shutdown -h 0` to `~/.bashrc`/`~/.zshrc` and created public GitHub repos to exfiltrate secrets | After any suspected compromise, audit `~/.bashrc`/`~/.zshrc`, the GitHub security log for unexpected repos, clean package caches, rotate tokens | s1ngularity postmortem ("Are You Affected?") <https://nx.dev/blog/s1ngularity-postmortem> | no — local machine state, not repo |
| 52 | Storing plaintext long-lived tokens/API keys on developer machines that plugin postinstall scripts can harvest | Use secure credential managers; avoid plaintext tokens; minimize local secret exposure | s1ngularity postmortem (Conclusion) <https://nx.dev/blog/s1ngularity-postmortem> ; OWASP Secrets Management CS <https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html> | no — local env, not repo |

---

## 6. CI security (the attack vector and hardening)

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|-------------|-----------------------------|----------------------|-----------------------|
| 53 | Using `pull_request_target` in a workflow that also handles untrusted PR content — it runs with the target branch's permissions and can read repo secrets (the root of the Nx breach) | Avoid `pull_request_target` for untrusted input; if unavoidable, never check out/execute PR code and never expose secrets | s1ngularity postmortem (The Vulnerability) <https://nx.dev/blog/s1ngularity-postmortem> ; GH Actions pull_request_target docs <https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#pull_request_target> | yes — `on: pull_request_target` in `.github/workflows/*.yml` |
| 54 | Echoing untrusted input (e.g. PR title `${{ github.event.pull_request.title }}`) directly into a `run:` step — shell/command injection (the exact injection point) | Never interpolate untrusted event data into `run:`; pass via `env:` and quote, or use safe actions | s1ngularity postmortem <https://nx.dev/blog/s1ngularity-postmortem> ; OWASP CI/CD Top 10 (CICD-SEC-4 Poisoned Pipeline Execution) <https://owasp.org/www-project-top-10-ci-cd-security-risks/> | yes — `${{ github.event.*.title/body/... }}` inside a `run:` block |
| 55 | Default "Read and write permissions" for the workflow `GITHUB_TOKEN` (older repos kept GitHub's pre-Feb-2023 default), giving any workflow full repo write | Set default workflow permissions to read-only; grant least-privilege `permissions:` per job | s1ngularity postmortem (Workflow permissions) <https://nx.dev/blog/s1ngularity-postmortem> ; OWASP CI/CD Top 10 (CICD-SEC-5 Insufficient PBAC) <https://owasp.org/www-project-top-10-ci-cd-security-risks/> | partial — repo-level default is a GitHub setting; per-workflow `permissions:` block presence is detectable |
| 56 | No explicit `permissions:` block in workflows, inheriting broad defaults | Add a least-privilege top-level `permissions:` (e.g. `contents: read`) to every workflow | OWASP CI/CD Top 10 (CICD-SEC-5) <https://owasp.org/www-project-top-10-ci-cd-security-risks/> | yes — workflow file with no `permissions:` key |
| 57 | Enabling `workflow_dispatch` on a publish workflow, allowing programmatic triggering via the API against an attacker-controlled branch | Restrict manual/dispatch triggers on publish workflows; gate publishing behind protected branches and approvals | s1ngularity postmortem (Manual workflow dispatch) <https://nx.dev/blog/s1ngularity-postmortem> | partial — `workflow_dispatch` on a job that runs publish is a signal |
| 58 | Publishing to npm with a long-lived `NPM_TOKEN` stored as a CI secret (the token that was exfiltrated and abused) | Use NPM Trusted Publishers (OIDC) instead of long-lived npm tokens; no static token to steal | s1ngularity postmortem ("no longer use NPM tokens ... switched to Trusted Publisher OIDC") <https://nx.dev/blog/s1ngularity-postmortem> ; NPM Trusted Publishers <https://docs.npmjs.com/trusted-publishers/> | yes — `NODE_AUTH_TOKEN`/`NPM_TOKEN` referenced in a publish workflow |
| 59 | Allowing external contributors' pipelines to run without approval, so a malicious PR reaches privileged execution | Require approval for all external-contributor pipeline runs (not just first-time contributors) | s1ngularity postmortem (Lessons Learned) <https://nx.dev/blog/s1ngularity-postmortem> | no — CI provider / repo setting |
| 60 | Publishing packages outside the reviewed CI pipeline (any path that can publish without review = full blast radius) | Enforce all publishing through CI with mandatory 2FA/manual approval; limit who/what can publish | s1ngularity postmortem (Lessons Learned) <https://nx.dev/blog/s1ngularity-postmortem> ; OWASP CI/CD Top 10 (CICD-SEC-1 Insufficient Flow Control) <https://owasp.org/www-project-top-10-ci-cd-security-risks/> | no — process/approval control |
| 61 | Unpinned third-party GitHub Actions (`uses: some/action@main` / `@v3` tag) that can be moved to malicious code | Pin actions to a full commit SHA | OWASP CI/CD Top 10 (CICD-SEC-4 Poisoned Pipeline Execution) <https://owasp.org/www-project-top-10-ci-cd-security-risks/> | yes — `uses:` with a branch/tag instead of a 40-char SHA |
| 62 | No `SECURITY.md` / responsible-disclosure path, delaying incident response | Add a `SECURITY.md` with a disclosure contact (Nx added `security@nrwl.io`) | s1ngularity postmortem (created SECURITY.md) <https://nx.dev/blog/s1ngularity-postmortem> ; SECURITY.md <https://github.com/nrwl/nx/blob/master/SECURITY.md> | yes — presence/absence of `SECURITY.md` |
| 63 | Read-write cache token usable by anyone with direct push to a protected branch, letting them write cache without code review | Require code review to merge to protected branches; do not allow direct pushes that can write the cache | Access Tokens (GitHub/GitLab "Can't someone change...") <https://nx.dev/docs/guides/nx-cloud/access-tokens> | no — branch-protection setting in the provider |

---

## Notes for keur rule authors

- Highest-signal, cleanly static rules (good first `.rule` candidates):
  - #1/#2/#3 — literal `nxCloudAccessToken` / `accessToken` / `nxCloudEncryptionKey` values in `nx.json` (secret + config category).
  - #5 — `nx-cloud.env` tracked or missing from `.gitignore`.
  - #7/#58 — token literals / `NPM_TOKEN` usage in `.github/workflows/*.yml`.
  - #20/#23/#46 — malformed `inputs` (`.sh`/`.bat` runtime, bare directory input, `absolute` working dir).
  - #24/#25/#26 — `@nx/enforce-module-boundaries` absent, `"warn"/"off"`, or empty `depConstraints`.
  - #31/#33/#35 — boolean option flags (`banTransitiveDependencies`, `enforceBuildableLibDependency`, `allowCircularSelfDependency`).
  - #48/#49 — `nx`/`@nx/*` version ranges and known-malicious pinned versions in `package.json`.
  - #53/#54/#56/#61 — workflow-level: `pull_request_target`, untrusted `${{ github.event.* }}` in `run:`, missing `permissions:`, unpinned `uses:`.
- "partial" items usually need project-graph or tag-design context; keur can flag the pattern and defer the final decision to the policy engine.
- "no" items are runtime/UI/provider-side and are out of scope for a source-file scanner (document them so keur does not falsely claim coverage).
