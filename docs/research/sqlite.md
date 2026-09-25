# SQLite & better-sqlite3 (Node) — Security Hardening Research

Research feed for keur rule development. For each item: the antipattern (the
insecure/wrong thing people do), the corresponding best practice (the correct
hardening), an authoritative source, and whether keur could detect it
**statically** in source code (yes / partial / no — no = runtime/config/DB-file
only).

## Sources (canonical)

- **OWASP SQLi Prevention** — SQL Injection Prevention Cheat Sheet.
  <https://cheatsheetseries.owasp.org/cheatsheets/SQL_Injection_Prevention_Cheat_Sheet.html>
- **OWASP QueryParam** — Query Parameterization Cheat Sheet.
  <https://cheatsheetseries.owasp.org/cheatsheets/Query_Parameterization_Cheat_Sheet.html>
- **OWASP DB** — Database Security Cheat Sheet.
  <https://cheatsheetseries.owasp.org/cheatsheets/Database_Security_Cheat_Sheet.html>
- **OWASP Node** — Node.js Security Cheat Sheet.
  <https://cheatsheetseries.owasp.org/cheatsheets/Nodejs_Security_Cheat_Sheet.html>
- **SQLite DADA** — SQLite "Defense Against The Dark Arts". <https://www.sqlite.org/security.html>
- **SQLite PRAGMA** — PRAGMA reference. <https://www.sqlite.org/pragma.html>
- **SQLite FK** — Foreign Key Support. <https://www.sqlite.org/foreignkeys.html>
- **SQLite WAL** — Write-Ahead Logging. <https://www.sqlite.org/wal.html>
- **SQLite ATTACH** — ATTACH DATABASE. <https://www.sqlite.org/lang_attach.html>
- **SQLite loadext** — Run-Time Loadable Extensions. <https://www.sqlite.org/loadext.html>
- **SQLite defensive** — SQLITE_DBCONFIG_DEFENSIVE / db_config flags.
  <https://www.sqlite.org/c3ref/c_dbconfig_defensive.html>
- **SQLite quote** — Query Language: quoting / SQL functions. <https://www.sqlite.org/lang_corefunc.html>
- **bsq3 API** — better-sqlite3 API docs. <https://github.com/WiseLibs/better-sqlite3/blob/master/docs/api.md>

> Note: SQLite is a C library with no user/role model and no network listener,
> so classic RDBMS controls (GRANT, network TLS, service accounts) do not apply.
> The relevant controls here are: SQL construction (injection), connection-open
> PRAGMAs/db_config flags, the OS-level db file (permissions, encryption at
> rest), untrusted schema/attach/extension surface, and better-sqlite3 API
> misuse. "Statically detectable" means keur can flag it from the source text
> (a call site, an API name, a string-built query, a missing PRAGMA at open).

---

## 1. Antipatterns → Best practices

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|-------------|------------------------------|---------------------|-----------------------|
| 1 | Building SQL by concatenating user input: `db.prepare("SELECT * FROM users WHERE name = '" + name + "'")` | Use a placeholder and bind: `db.prepare("SELECT * FROM users WHERE name = ?").get(name)` | OWASP SQLi Prevention (Defense Option 1) <https://cheatsheetseries.owasp.org/cheatsheets/SQL_Injection_Prevention_Cheat_Sheet.html> | yes — string concat/template literal inside `prepare(`/`exec(` |
| 2 | Template literals interpolating variables into SQL: ``db.prepare(`SELECT * FROM t WHERE id = ${id}`)`` | Parameterize with `?`/`@name`/`:name`/`$name` and pass values to `.run/.get/.all` | bsq3 API "Binding Parameters" <https://github.com/WiseLibs/better-sqlite3/blob/master/docs/api.md> | yes — `${...}` inside a template passed to `prepare`/`exec` |
| 3 | Passing user input through `db.exec(userSql)` (exec runs arbitrary multi-statement SQL, no binding) | Use `db.prepare()` with bound params; reserve `.exec()` for trusted static SQL (migrations from files) | bsq3 API `.exec()` ("performs worse and is less safe … only when you need to execute SQL from an external source") <https://github.com/WiseLibs/better-sqlite3/blob/master/docs/api.md> | yes — `.exec(` with a non-literal / concatenated arg |
| 4 | String-concatenating a value into a `PRAGMA` (e.g. `db.pragma("cache_size = " + n)` where `n` is user input) | Validate/whitelist to a numeric/enum before it reaches `.pragma()`; never pass raw user input | bsq3 API `.pragma()`; OWASP SQLi Prevention (allow-list) | partial — flag concat into `.pragma(` |
| 5 | Interpolating a **table name** from user input into the query string | Map user value through a fixed allow-list `switch`/lookup to a hardcoded table name; table/column names cannot be bound params | OWASP SQLi Prevention (Defense Option 3, "Safe Table Name Validation") <https://cheatsheetseries.owasp.org/cheatsheets/SQL_Injection_Prevention_Cheat_Sheet.html> | partial — detect identifier position built from a variable |
| 6 | Interpolating a **column name** from user input | Same allow-list mapping to hardcoded column identifiers; reject anything not in the set | OWASP SQLi Prevention (Defense Option 3) | partial |
| 7 | Interpolating `ORDER BY <col> <dir>` (sort column / direction) from user input | Convert to boolean/enum first, then select a hardcoded fragment: `order by Salary ` + `(asc ? "ASC" : "DESC")` | OWASP SQLi Prevention ("Sample of Safer Dynamic Query Generation") | partial |
| 8 | Interpolating `LIMIT`/`OFFSET` values via string concat | Bind them as parameters (`LIMIT ? OFFSET ?`) — they are ordinary values | OWASP QueryParam <https://cheatsheetseries.owasp.org/cheatsheets/Query_Parameterization_Cheat_Sheet.html> | yes — concat into `LIMIT`/`OFFSET` |
| 9 | Relying on manual escaping / hand-rolled quote functions to sanitize input | Do not escape-and-concat; use parameterized queries. Escaping is "STRONGLY DISCOURAGED" and cannot be guaranteed safe | OWASP SQLi Prevention (Defense Option 4) | partial — flag custom `escapeSql`/`replace("'","''")` helpers |
| 10 | Using `IN (...)` built by joining user values into one string | Generate the exact number of `?` placeholders and bind each element | OWASP QueryParam | partial — `IN (` followed by concatenated `join` |
| 11 | Foreign key constraints assumed enforced but never enabled (SQLite defaults `foreign_keys=OFF`) | Run `PRAGMA foreign_keys = ON` on **every** connection at open; do not rely on the default | SQLite PRAGMA `foreign_keys` ("default setting … is OFF … applications should set the flag as required and not depend on the default") <https://www.sqlite.org/pragma.html> ; SQLite FK <https://www.sqlite.org/foreignkeys.html> | partial — presence of open handler without a `foreign_keys` pragma |
| 12 | Setting `PRAGMA foreign_keys` inside a transaction (silently a no-op there) | Set `foreign_keys` before any `BEGIN`/`SAVEPOINT`, right after opening the connection | SQLite PRAGMA `foreign_keys` ("This pragma is a no-op within a transaction") | partial |
| 13 | Using `PRAGMA defer_foreign_keys=0` mid-transaction to force a commit past FK violations | Do not reset deferred-FK tracking to escape a violation; fix the data or use `DEFERRABLE INITIALLY DEFERRED` intentionally | SQLite PRAGMA `defer_foreign_keys` ("Setting defer_foreign_keys=0 resets … possible to commit … even if there are foreign key violations") | yes — literal `defer_foreign_keys = 0` |
| 14 | Concurrent readers/writers on the default `DELETE` rollback journal causing writer-blocks-reader lock contention | Enable `PRAGMA journal_mode = WAL` for concurrent read/write workloads | SQLite WAL <https://www.sqlite.org/wal.html> ; SQLite PRAGMA `journal_mode` | partial — missing `journal_mode=WAL` where concurrency assumed |
| 15 | `PRAGMA journal_mode = OFF` in production (disables rollback journal; crash mid-txn corrupts the DB) | Keep a rollback journal (`DELETE`/`TRUNCATE`) or use `WAL`; never `OFF` for durable data | SQLite PRAGMA `journal_mode` ("if the application crashes … the database file will very likely go corrupt … disabled when DEFENSIVE is enabled") | yes — literal `journal_mode = OFF`/`= MEMORY` |
| 16 | `PRAGMA synchronous = OFF` on durable data (DB "might become corrupted" on OS crash/power loss) | Leave `synchronous` at `FULL` (rollback) or use `NORMAL` only with WAL, accepting the documented durability trade-off | SQLite PRAGMA `synchronous` ("might become corrupted if the operating system crashes … before that data has been written") | yes — literal `synchronous = OFF` |
| 17 | WAL mode with `synchronous=NORMAL` treated as fully durable (a committed txn can roll back on power loss) | Use `synchronous=EXTRA`/`FULL` when durability across power loss matters; document the NORMAL trade-off | SQLite PRAGMA `synchronous` ("A transaction committed in WAL mode with synchronous=NORMAL might roll back following a power loss") | partial |
| 18 | Sensitive data deleted with plain `DELETE` leaving recoverable content in freelist pages | Enable `PRAGMA secure_delete = ON` before delete/update, or run `VACUUM` after, to overwrite content with zeros | SQLite PRAGMA `secure_delete` ("Applications that wish to avoid leaving forensic traces … should enable the secure_delete pragma prior to … the delete") | partial — presence of DELETE-heavy code without `secure_delete`/`VACUUM` |
| 19 | Assuming `secure_delete=FAST` scrubs everything (it leaves forensic traces on freelist pages) | For full scrubbing use `secure_delete=ON` (full), not `FAST`; know FTS3/FTS5 shadow tables still leave traces | SQLite PRAGMA `secure_delete` ("fast … leaving forensic traces on freelist pages"; FTS shadow-table limitation) | yes — literal `secure_delete = fast` where full scrub intended |
| 20 | `ATTACH DATABASE` on a path/URI derived from untrusted input (attacker points at arbitrary file or overlays schema) | Never attach untrusted paths; if untrusted input is unavoidable, reduce `SQLITE_LIMIT_ATTACH` to 0 for the connection | SQLite DADA (LIMIT_ATTACH high-security value `0`) <https://www.sqlite.org/security.html> ; SQLite ATTACH <https://www.sqlite.org/lang_attach.html> | partial — `ATTACH` with a variable/concatenated path |
| 21 | Opening an SQLite DB file received from an untrusted source without any hardening | Treat untrusted DB files as hostile: run `PRAGMA integrity_check` first and reject on error, and apply the DADA precautions | SQLite DADA §1.2 <https://www.sqlite.org/security.html> | partial |
| 22 | Leaving `PRAGMA trusted_schema=ON` (default) so unaudited SQL functions / vtables in a schema can run via views/triggers/CHECK/DEFAULT | Run `PRAGMA trusted_schema=OFF` on every connection at open (or compile `-DSQLITE_TRUSTED_SCHEMA=0`) | SQLite PRAGMA `trusted_schema` ("all applications are encouraged to switch this setting off on every database connection as soon as … opened"); SQLite DADA §1.2(9b) | partial — missing `trusted_schema=OFF` at open |
| 23 | Registering custom SQL functions that have side effects / leak data without `directOnly`, so a malicious schema can invoke them | Register with `{ directOnly: true }` so the function is only callable from top-level SQL, not from VIEW/TRIGGER/CHECK/DEFAULT | bsq3 API `.function()` (`options.directOnly`) <https://github.com/WiseLibs/better-sqlite3/blob/master/docs/api.md> ; SQLite DADA §1.2(9d) (SQLITE_DIRECTONLY) | yes — `db.function(...)` without `directOnly` |
| 24 | Registering custom aggregates/virtual tables with side effects without `directOnly` | Set `directOnly: true` on `.aggregate()` and `.table()` too | bsq3 API `.aggregate()` / `.table()` ; SQLite DADA §1.2(9d) (SQLITE_VTAB_DIRECTONLY) | yes — `.aggregate(`/`.table(` without `directOnly` |
| 25 | Declaring a UDF `deterministic: true` when it actually is not (network/random/time), corrupting expression-index/query-plan results | Only set `deterministic: true` for truly deterministic functions | bsq3 API `.function()` (`options.deterministic`) | partial — `deterministic:true` on a func using Date/Math.random/fetch |
| 26 | `db.loadExtension()` on a path derived from untrusted input, or loading unvetted native extensions | Load only vetted, version-matched extensions from trusted fixed paths; never from user input | bsq3 API `.loadExtension()` ("your responsibility to make sure the extensions you load are compiled/linked against a … compatible" SQLite) ; SQLite loadext <https://www.sqlite.org/loadext.html> | yes — `.loadExtension(` with a variable arg |
| 27 | Not enabling `SQLITE_DBCONFIG_DEFENSIVE`, so malicious SQL can corrupt the DB file via `writable_schema`/`PRAGMA schema_version` etc. | Enable defensive mode (`SQLITE_DBCONFIG_DEFENSIVE`) on connections that accept untrusted SQL | SQLite DADA §1.1(1) <https://www.sqlite.org/security.html> | no — C-API/config only (bsq3 exposes no direct flag) |
| 28 | `PRAGMA writable_schema=ON` in application code (lets ordinary DML corrupt `sqlite_schema`) | Never enable `writable_schema` in app code; it "can easily result in a corrupt database file" | SQLite PRAGMA `writable_schema` ("misuse … can easily result in a corrupt database file") | yes — literal `writable_schema` |
| 29 | Overwriting `PRAGMA schema_version = N` to force stale-schema execution | Never write `schema_version`; only reading it is safe | SQLite PRAGMA `schema_version` ("changing … can cause problems … can lead to … database corruption") | yes — `schema_version =` assignment |
| 30 | No SQL-input limits when accepting untrusted SQL (deep recursion → stack overflow / DoS) | Reduce `sqlite3_limit()` values (LIMIT_SQL_LENGTH, LIMIT_EXPR_DEPTH, LIMIT_ATTACH, LIMIT_VARIABLE_NUMBER, …) to high-security values | SQLite DADA §1.1(2) with the limits table <https://www.sqlite.org/security.html> | no — C-API/config only |
| 31 | No memory ceiling on connections handling untrusted SQL (memory-exhaustion DoS) | Set `sqlite3_hard_heap_limit64()` to observed peak + margin | SQLite DADA §1.1(5) | no — C-API only |
| 32 | No statement authorizer while accepting untrusted SQL (attacker can `DROP`/`CREATE`/read anything) | Install `sqlite3_set_authorizer()` to deny DDL / restrict scope | SQLite DADA §1.1(3) | no — C-API only |
| 33 | Long-running untrusted queries with no interrupt (CPU DoS) | Use `sqlite3_progress_handler()` or a watchdog thread calling `sqlite3_interrupt()` to abort runaway statements | SQLite DADA §1.1(4) | no — runtime only |
| 34 | Enabling memory-mapped I/O when reading untrusted DB files (a bug becomes a memory-safety issue) | Keep `PRAGMA mmap_size = 0` for untrusted DB files | SQLite DADA §1.2 (extra defenses: "make sure that PRAGMA mmap_size=0") | yes — `mmap_size =` nonzero with untrusted files |
| 35 | Not enabling `cell_size_check` when opening untrusted DB files (corruption spreads before detection) | Set `PRAGMA cell_size_check = ON` for untrusted DB files | SQLite DADA §1.2 (extra defenses) ; SQLite PRAGMA `cell_size_check` | partial |
| 36 | Leaving triggers/views enabled on untrusted DB files when the app doesn't use them | Disable with `SQLITE_DBCONFIG_ENABLE_TRIGGER=0` and `SQLITE_DBCONFIG_ENABLE_VIEW=0` | SQLite DADA §1.2(10) | no — C-API/config only |
| 37 | SQLite DB file created with world-readable/writable permissions (secrets readable by other local users) | Restrict file mode (e.g. `0600`) and directory perms; DB, `-wal`, `-shm`, `-journal` sidecars all leak data | OWASP DB Cheat Sheet (file permissions / least privilege) <https://cheatsheetseries.owasp.org/cheatsheets/Database_Security_Cheat_Sheet.html> | partial — `new Database(path)` with no umask/chmod guard |
| 38 | Storing the SQLite file inside the web/document root (directly downloadable) | Place the DB file outside any served directory | OWASP DB Cheat Sheet <https://cheatsheetseries.owasp.org/cheatsheets/Database_Security_Cheat_Sheet.html> | partial — DB path under `public/`/`www`/`static` |
| 39 | Sensitive data at rest in a plaintext SQLite file (no encryption) | Use an encrypted variant (SQLCipher / SEE) or OS-level encryption; SQLite core stores plaintext | OWASP DB Cheat Sheet (encryption at rest) | no — cannot tell from source alone |
| 40 | Running as a DB "readonly" reader but opening read/write anyway | Open with `new Database(path, { readonly: true })` for read-only workloads (least privilege) | bsq3 API `new Database()` (`options.readonly`) <https://github.com/WiseLibs/better-sqlite3/blob/master/docs/api.md> | yes — read-only usage opened without `readonly:true` |
| 41 | Auto-creating a DB file when a missing file should be an error (typo/path attack silently creates a new empty DB) | Pass `{ fileMustExist: true }` when the file must already exist | bsq3 API `new Database()` (`options.fileMustExist`) | yes — open of existing-expected DB without `fileMustExist` |
| 42 | Logging every executed SQL (incl. bound values) via `{ verbose: console.log }` in production (leaks PII/secrets to logs) | Disable `verbose` in production; if needed, redact values | bsq3 API `new Database()` (`options.verbose` — "gets called with every SQL string executed") | yes — `verbose:` set to a logger in prod config |
| 43 | Concatenating values into `Statement.toString()` output and re-executing (expanded SQL is not re-safe) | Never round-trip expanded SQL back into `prepare/exec`; keep placeholders and bind | bsq3 API `.toString()` (returns *expanded* SQL with params substituted) | partial — `.toString()` result fed into `prepare`/`exec` |
| 44 | Using `.exec()` to run a whole user-supplied `.sql` migration blindly (multi-statement, no rollback on failure) | Run trusted migrations only; wrap in a transaction and roll back manually on error (exec "must rollback changes manually") | bsq3 API `.exec()` | partial |
| 45 | Mixing raw `BEGIN`/`COMMIT`/`ROLLBACK` statements inside a `db.transaction()` function (unsupported; corrupts txn state) | Manage transactions either with `db.transaction()` **or** with raw statements, never both | bsq3 API `.transaction()` Caveats ("using raw COMMIT or ROLLBACK … inside a transaction function is not supported") | yes — `.run('COMMIT')`/`ROLLBACK` inside a `transaction(` callback |
| 46 | Using `db.transaction()` with an `async` function (txn commits before the await resolves → no atomicity) | Keep transaction callbacks synchronous; never `await` inside them | bsq3 API `.transaction()` Caveats ("Transaction functions do not work with async functions") | yes — `async` callback passed to `.transaction(` |
| 47 | Catching an SQLite error inside a txn and continuing to run SQL, assuming it's still in the same transaction | On error inside a txn, check `db.inTransaction` and re-throw if the txn was force-rolled-back | bsq3 API `.transaction()` Caveats (`if (!db.inTransaction) throw err;`) | partial |
| 48 | Not enforcing least privilege on the DB account/schema (over-broad access magnifies an injection) | Apply least privilege: separate read vs write access, use views to narrow field-level exposure | OWASP SQLi Prevention (Additional Defenses: Least Privilege; SQL Views) <https://cheatsheetseries.owasp.org/cheatsheets/SQL_Injection_Prevention_Cheat_Sheet.html> | no — N/A to SQLite core (no user model); design/config only |
| 49 | Treating a local DB file that another security domain could write as trusted | Treat any file ever writable by another security domain as suspect and apply untrusted-file defenses | SQLite DADA §1.2 ("any database file which might have ever been writable by an agent in a different security domain should be treated as suspect") | no — design/config only |
| 50 | Relying on default SQLite input limits when accepting hostile SQL on a low-stack device | On constrained targets set `LIMIT_EXPR_DEPTH`/`LIMIT_TRIGGER_DEPTH` to small values (e.g. 10) to prevent stack-overflow DoS | SQLite DADA §1.1(2) note on deep recursion / CPU stack overflow | no — compile/runtime config only |
| 51 | Not validating the DB against schema/integrity before use in high-security contexts | Run `PRAGMA integrity_check`/`quick_check` first and refuse files with errors | SQLite DADA §1.2 (extra defenses) ; SQLite PRAGMA `integrity_check` | partial |
| 52 | Passing bound parameters into `CREATE VIRTUAL TABLE` module arguments (unsupported — encourages string building of DDL) | Parse/validate module-argument strings yourself; do not build DDL from unvalidated input | bsq3 API `.table()` ("SQLite does not allow bound parameters inside module arguments") | partial |

---

## 2. keur rule candidates (highest static-detection value)

These map directly to `.rule` `pattern`/`presence`/`absence` engines and are the
strongest candidates because they are visible in source text:

- **Injection (pattern):** `\.(prepare|exec)\s*\(\s*[`"'][^)]*(\+|\$\{)` — SQL
  built with `+` concat or `${}` interpolation inside `prepare(`/`exec(`
  (rows 1–3, 8, 10). Highest priority; maps to OWASP SQLi Prevention.
- **Dangerous PRAGMAs (pattern):** literal `journal_mode\s*=\s*OFF`,
  `synchronous\s*=\s*OFF`, `writable_schema`, `schema_version\s*=`,
  `defer_foreign_keys\s*=\s*0`, `secure_delete\s*=\s*fast` (rows 13, 15, 16, 19,
  28, 29).
- **Untrusted surface (pattern):** `\.loadExtension\s*\(` with a non-literal arg,
  `ATTACH\b` with a variable path (rows 20, 26).
- **UDF misuse (pattern/absence):** `db\.function\(` / `db\.aggregate\(` /
  `db\.table\(` **without** `directOnly` (rows 23, 24).
- **Open-option hygiene (presence/absence):** `new Database(` — flag
  `verbose:` set to a logger (row 42); recommend `readonly`/`fileMustExist`
  where applicable (rows 40, 41).
- **Transaction misuse (pattern):** `async` callback into `.transaction(`, and
  `COMMIT`/`ROLLBACK` literals inside a transaction callback (rows 45, 46).
- **Connection-open PRAGMAs (absence, composite/facts — ADR-009):** a file that
  opens a DB but never issues `foreign_keys = ON` / `trusted_schema = OFF`
  (rows 11, 12, 22). Best expressed as a composite/fact rule, hence *partial*.

Runtime/config-only items (rows 27, 30–33, 36, 39, 48–50) are **not** statically
detectable from JS/TS source and should be documented as guidance, not rules.
