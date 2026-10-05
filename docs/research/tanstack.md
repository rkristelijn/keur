# TanStack Query & TanStack Table — Security & Correctness Antipatterns and Best Practices

Research input for keur rule development. Every item is backed by an authoritative
source: the TanStack official docs (Query + Table), TkDodo's blog (Dominik Dorfmeister,
official React Query maintainer), and OWASP cheat sheets where the concern is a
classic web-security issue (XSS, sensitive data storage).

Scope note: TanStack Query and TanStack Table are *headless* client libraries. They
have almost no attack surface of their own; the risk is in how developers wire them
into an app — caching sensitive data, persisting it to disk, rendering untrusted
cell values as HTML, or leaking data across users. That framing drives the entries
below. "Statically detectable" means whether keur could plausibly write a
source-code rule (yes / partial / no = runtime/config-only).

Sources referenced (short name → URL):

- **TQ:defaults** — TanStack Query, Important Defaults — <https://tanstack.com/query/latest/docs/framework/react/guides/important-defaults>
- **TQ:keys** — TanStack Query, Query Keys — <https://tanstack.com/query/latest/docs/framework/react/guides/query-keys>
- **TQ:retries** — TanStack Query, Query Retries — <https://tanstack.com/query/latest/docs/framework/react/guides/query-retries>
- **TQ:invalidation** — TanStack Query, Invalidation from Mutations — <https://tanstack.com/query/latest/docs/framework/react/guides/invalidations-from-mutations>
- **TQ:mutations** — TanStack Query, Mutations — <https://tanstack.com/query/latest/docs/framework/react/guides/mutations>
- **TQ:persist** — TanStack Query, persistQueryClient plugin — <https://tanstack.com/query/latest/docs/framework/react/plugins/persistQueryClient>
- **TQ:caching** — TanStack Query, Caching — <https://tanstack.com/query/latest/docs/framework/react/guides/caching>
- **TQ:queries** — TanStack Query, Queries — <https://tanstack.com/query/latest/docs/framework/react/guides/queries>
- **TQ:functions** — TanStack Query, Query Functions — <https://tanstack.com/query/latest/docs/framework/react/guides/query-functions>
- **TQ:eslint** — TanStack Query, ESLint Plugin (exhaustive-deps, stable-query-client) — <https://tanstack.com/query/latest/docs/eslint/eslint-plugin-query>
- **TQ:waterfalls** — TanStack Query, Performance & Request Waterfalls — <https://tanstack.com/query/latest/docs/framework/react/guides/request-waterfalls>
- **TT:data** — TanStack Table, Data Guide (stable references) — <https://tanstack.com/table/latest/docs/guide/data>
- **TT:flexrender** — TanStack Table, FlexRender — <https://tanstack.com/table/latest/docs/framework/react/guide/flex-render>
- **TT:state** — TanStack Table, Table State — <https://tanstack.com/table/latest/docs/framework/react/guide/table-state>
- **TT:virtual** — TanStack Table, Virtualization — <https://tanstack.com/table/latest/docs/framework/react/guide/virtualization>
- **TT:serverside** — TanStack Table, Client-Side vs Server-Side — <https://tanstack.com/table/latest/docs/guide/client-side-vs-server-side>
- **TkDodo:keys** — Effective React Query Keys — <https://tkdodo.eu/blog/effective-react-query-keys>
- **TkDodo:errors** — React Query Error Handling — <https://tkdodo.eu/blog/react-query-error-handling>
- **TkDodo:invalidation** — Automatic Query Invalidation after Mutations — <https://tkdodo.eu/blog/automatic-query-invalidation-after-mutations>
- **TkDodo:context** — Leveraging the Query Function Context — <https://tkdodo.eu/blog/leveraging-the-query-function-context>
- **TkDodo:statemanager** — React Query as a State Manager — <https://tkdodo.eu/blog/react-query-as-a-state-manager>
- **TkDodo:practical** — Practical React Query — <https://tkdodo.eu/blog/practical-react-query>
- **TkDodo:offline** — Offline React Query — <https://tkdodo.eu/blog/offline-react-query>
- **OWASP:XSS** — OWASP Cross Site Scripting Prevention Cheat Sheet — <https://cheatsheetseries.owasp.org/cheatsheets/Cross_Site_Scripting_Prevention_Cheat_Sheet.html>
- **OWASP:DOMXSS** — OWASP DOM based XSS Prevention Cheat Sheet — <https://cheatsheetseries.owasp.org/cheatsheets/DOM_based_XSS_Prevention_Cheat_Sheet.html>
- **OWASP:HTML5** — OWASP HTML5 Security Cheat Sheet (Web Storage) — <https://cheatsheetseries.owasp.org/cheatsheets/HTML5_Security_Cheat_Sheet.html>
- **OWASP:A01** — OWASP Top 10 2021 A01 Broken Access Control — <https://owasp.org/Top10/A01_2021-Broken_Access_Control/>

---

## TanStack Query (react-query)

### Antipatterns → Best practices

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|---|---|---|---|
| 1 | Persisting the whole `QueryClient` cache to `localStorage`/`sessionStorage` via `createSyncStoragePersister({ storage: window.localStorage })` without filtering — sensitive/PII query data lands on disk in plaintext, readable by any script (XSS) and left behind on shared machines. | Use `persistQueryClient` `dehydrateOptions.shouldDehydrateQuery` to allowlist only non-sensitive queries; prefer IndexedDB or in-memory; never persist auth/PII query keys. | TQ:persist (<https://tanstack.com/query/latest/docs/framework/react/plugins/persistQueryClient>); OWASP:HTML5 (<https://cheatsheetseries.owasp.org/cheatsheets/HTML5_Security_Cheat_Sheet.html>) | partial (detect `createSyncStoragePersister`/`persistQueryClient` with `window.localStorage` and no `dehydrateOptions`) |
| 2 | Storing access/refresh tokens or JWTs as query data (`useQuery({ queryKey: ['token'], ... })`) so they sit in the in-memory cache and any persister. | Keep tokens out of the Query cache; store short-lived tokens in memory only, refresh via httpOnly cookies handled by the transport layer. | TQ:persist; OWASP:HTML5 | partial (query key literals like `'token'`, `'jwt'`, `'accessToken'`) |
| 3 | Putting secrets/PII (email, SSN, API keys) into the `queryKey` array — keys are hashed deterministically and shown verbatim in React Query Devtools and any persisted cache. | Keep `queryKey` limited to identifiers needed for caching (ids, filter params); never embed secrets or raw PII in the key. | TQ:keys (<https://tanstack.com/query/latest/docs/framework/react/guides/query-keys>); TkDodo:keys (<https://tkdodo.eu/blog/effective-react-query-keys>) | partial (string/`process.env`/token-like literals inside `queryKey`) |
| 4 | Reusing the **same `queryKey`** for `useQuery` and `useInfiniteQuery` — they share one cache entry with incompatible shapes, causing data corruption. | Give infinite queries a distinct key (e.g. `['infiniteTodos']` vs `['todos']`). | TkDodo:keys; TQ:keys | partial (same key literal on `useQuery` and `useInfiniteQuery`) |
| 5 | Query depends on state (`filters`, `id`, `userId`) but that state is **not in the `queryKey`** — stale/wrong data served, and one user can see another user's cached data. | Put every input the `queryFn` reads into the `queryKey`; treat the key like a `useEffect` dependency array. | TkDodo:keys; TkDodo:practical (<https://tkdodo.eu/blog/practical-react-query>); TQ:eslint exhaustive-deps (<https://tanstack.com/query/latest/docs/eslint/eslint-plugin-query>) | partial (`queryFn` closes over a variable absent from `queryKey`; this is what the `@tanstack/eslint-plugin-query` `exhaustive-deps` rule detects) |
| 6 | Calling `refetch(newParams)` to fetch with different parameters. `refetch` only re-runs with the **same** parameters. | Drive the fetch by changing client state that is in the `queryKey`; let the key change trigger the refetch. | TkDodo:keys | partial (arguments passed to `refetch(...)`) |
| 7 | Not invalidating related queries after a mutation, so the UI shows stale data after a create/update/delete. | Call `queryClient.invalidateQueries({ queryKey })` in the mutation's `onSuccess`/`onSettled`, or use a global `MutationCache` `onSuccess` for automatic invalidation. | TQ:invalidation (<https://tanstack.com/query/latest/docs/framework/react/guides/invalidations-from-mutations>); TkDodo:invalidation (<https://tkdodo.eu/blog/automatic-query-invalidation-after-mutations>) | partial (a `useMutation` with no `invalidateQueries`/`setQueryData` in its callbacks) |
| 8 | Flat, unstructured query keys (`['todosList']`, `['todosDetail1']`) that cannot be fuzzy-matched, so invalidation must target each key by hand and misses some. | Structure keys most-generic→most-specific (`['todos','list',{filters}]`) and use a per-feature **query key factory**; invalidate whole subtrees with `['todos']`. | TkDodo:keys | partial (string-concatenated keys vs array hierarchy) |
| 9 | Manually re-declaring query key arrays at each call site — drift between the fetch key and the invalidation key means invalidation silently no-ops. | Define one query key factory object per feature and reference it everywhere. | TkDodo:keys | partial (duplicate inline key literals across files) |
| 10 | Leaving `retry` at the default `3` for queries that fail with `401`/`403` — the app hammers the auth endpoint 4× with backoff on every unauthorized request. | Provide `retry: (failureCount, error) => error.status !== 401 && error.status !== 403` (or `retry: false` for auth-guarded queries). | TQ:retries (<https://tanstack.com/query/latest/docs/framework/react/guides/query-retries>) | partial (a `queryFn` hitting auth-guarded endpoints with `retry` unset/`true`) |
| 11 | Setting `retry: true` (infinite retries) — a persistently failing or 4xx request retries forever, a self-inflicted DoS on the backend. | Use a finite integer or a predicate that stops on non-retryable (4xx) errors. | TQ:retries | yes (`retry: true` literal) |
| 12 | Mutations retried by default on non-idempotent operations. Mutations do **not** retry by default; overriding `retry` on a `useMutation` that does a non-idempotent POST can double-submit. | Leave mutation `retry` off (default), or make the endpoint idempotent (idempotency key) before enabling retries. | TQ:mutations (<https://tanstack.com/query/latest/docs/framework/react/guides/mutations>); TQ:retries | yes (`retry` set on `useMutation`) |
| 13 | No error handling at all — only reading `data`, ignoring `isError`/`error`, so failures render as blank/`undefined` and mask security-relevant failures (e.g. a 403). | Handle the `error`/`isError` state, or use `throwOnError` with an Error Boundary. | TkDodo:errors (<https://tkdodo.eu/blog/react-query-error-handling>); TQ:queries (<https://tanstack.com/query/latest/docs/framework/react/guides/queries>) | partial (`useQuery` whose destructure omits `error`/`isError` and has no `throwOnError`) |
| 14 | `queryFn` uses `fetch()` and returns without checking `response.ok` — `fetch` does not reject on 4xx/5xx, so error responses are cached as valid data. | In the `queryFn`, throw on `!response.ok` so React Query treats it as an error (axios does this automatically). | TkDodo:errors; TQ:functions (<https://tanstack.com/query/latest/docs/framework/react/guides/query-functions>) | partial (a `queryFn` with `fetch(...)` and no `res.ok`/status check before `.json()`) |
| 15 | Per-observer `onError` toast logic duplicated so every component using the hook fires a toast — noisy, and error-reporting fires N times per failure. | Put error reporting in a global `QueryCache`/`MutationCache` `onError` so it runs once per request. | TkDodo:errors | partial (`onError` on `useQuery`/custom hook used in multiple places) |
| 16 | `staleTime` left at default `0` for sensitive or expensive data, causing constant background refetches on mount/focus/reconnect (extra load, extra token exposure). | Set an explicit `staleTime` matching the data's volatility (e.g. `2 * 60 * 1000`); use `'static'`/`Infinity` for boot-time reference data. | TQ:defaults (<https://tanstack.com/query/latest/docs/framework/react/guides/important-defaults>) | partial (heuristic: no `staleTime` configured globally or per-query) |
| 17 | Long or `Infinity` `gcTime` on sensitive per-user data — after logout the previous user's data lingers in memory (default gc is only 5 min). | Keep `gcTime` bounded for sensitive queries and call `queryClient.clear()` on logout to drop cached data immediately. | TQ:defaults; TQ:caching (<https://tanstack.com/query/latest/docs/framework/react/guides/caching>) | partial (`gcTime: Infinity`; also detect absence of `queryClient.clear()`/`removeQueries` on logout) |
| 18 | Not clearing the cache on logout/user switch — the next user (or the same browser) can read the prior session's cached queries. | Call `queryClient.clear()` (and remove any persisted cache via the persister's `removeClient`) in the logout flow. | TQ:caching; TQ:persist | partial (logout handler present but no `queryClient.clear()`) |
| 19 | Setting `gcTime` **lower** than the persister's `maxAge` — the cache is garbage-collected before it can be restored, silently breaking offline behavior. | Set `gcTime` >= persister `maxAge` (docs: if `maxAge` is 24h, `gcTime` should be 24h+). | TQ:persist | partial (compare `gcTime` and `maxAge` numeric literals when both present) |
| 20 | Fetching imperatively inside render or an event with raw `fetch`/`axios` instead of `useQuery`, bypassing dedupe/caching and creating request waterfalls. | Model server state as queries; let React Query dedupe and cache. | TkDodo:statemanager (<https://tkdodo.eu/blog/react-query-as-a-state-manager>); TQ:waterfalls (<https://tanstack.com/query/latest/docs/framework/react/guides/request-waterfalls>) | partial (bare `fetch(`/`axios(` in a component body/effect alongside React Query usage) |
| 21 | Creating `new QueryClient()` inside the component render body — a new client (and empty cache) is created on every render, losing cache and leaking memory. | Create the `QueryClient` once (module scope or `useState(() => new QueryClient())`); the ESLint `stable-query-client` rule enforces this. | TQ:eslint stable-query-client (<https://tanstack.com/query/latest/docs/eslint/eslint-plugin-query>) | yes (`new QueryClient(` inside a component function body) |
| 22 | Copying `useQuery` `data` into local `useState` and mutating it — desyncs from the cache; users act on stale/inconsistent data. | Treat the query cache as the single source of truth for server state; derive with `select`, don't fork into `useState`. | TkDodo:statemanager; TkDodo:practical | partial (`useState` initialized from a query's `data`) |
| 23 | `enabled` not set on a dependent query, so it fires with `undefined` params (e.g. `userId` not yet loaded), producing a wrong/unauthorized request. | Gate dependent queries with `enabled: !!param`. | TQ:queries; TkDodo:context (<https://tkdodo.eu/blog/leveraging-the-query-function-context>) | partial (query key contains a value that may be undefined and `enabled` is missing) |
| 24 | Ignoring the `QueryFunctionContext` and closing over params instead of reading them from the context, so the key and the fetched params can diverge. | Read params from the `queryKey` via the `QueryFunctionContext` (`({ queryKey }) => ...`). | TkDodo:context | partial (queryFn ignores its context arg while using outer-scope params) |
| 25 | Disabling `structuralSharing` needlessly, or relying on it with non-JSON data (Dates/Files) where it can't detect changes — subtle stale-render bugs. | Keep default `structuralSharing`; provide a custom function only for non-JSON payloads. | TQ:defaults | yes (`structuralSharing: false` literal) |
| 26 | Using `persistQueryClientSubscribe`/`PersistQueryClientProvider` unconditionally (no "remember me" gate) so every user's cache is written to disk. | Only subscribe the persister when the user opts in; unsubscribe and bust the cache on logout. | TQ:persist | partial (persist wired at app root with no conditional) |
| 27 | No `buster` string on the persisted client, so after a deploy that changes data shapes the app hydrates incompatible cached data. | Pass a `buster` (e.g. build hash) to `persistQueryClient*`; mismatched cache is discarded. | TQ:persist | yes (`persistQueryClient(` call with no `buster` key) |
| 28 | Logging or reporting the raw `error` object to third-party trackers, leaking response bodies/PII that came back with a 4xx. | Sanitize errors before reporting; report in the global cache `onError` with scrubbing. | TkDodo:errors | partial (error passed straight into a logging/telemetry call) |
| 29 | Treating a background-refetch failure as a hard error and unmounting the list (checking only `isError`), hiding still-valid data and worsening UX during outages. | Show a toast for background errors while keeping stale data; only hard-fail on initial load. | TkDodo:errors | no (runtime UX behavior) |
| 30 | Using `refetchInterval` polling on sensitive endpoints without pausing when the tab is inactive, leaking data/tokens on a backgrounded tab. | Rely on default focus-pausing; only set `refetchIntervalInBackground: true` deliberately. | TQ:retries (background retry behavior) | yes (`refetchIntervalInBackground: true` literal) |

### Best-practice checklist (positive form, condensed)

These restate the right-hand column so keur can also assert *presence* of good patterns.

- Array query keys, structured generic→specific, via a per-feature key factory. (TkDodo:keys)
- Every `queryFn` input appears in the `queryKey`. (TkDodo:keys, TQ:eslint)
- Mutations invalidate/patch affected queries in `onSuccess`/`onSettled`. (TQ:invalidation)
- `retry` predicate stops on 401/403; no `retry: true`. (TQ:retries)
- `queryFn` throws on `!res.ok` when using `fetch`. (TkDodo:errors)
- Explicit `staleTime`/`gcTime` chosen per data volatility. (TQ:defaults)
- `queryClient.clear()` on logout; persisted cache busted. (TQ:caching, TQ:persist)
- One stable `QueryClient` instance. (TQ:eslint stable-query-client)
- Global `QueryCache`/`MutationCache` `onError` for once-per-request reporting. (TkDodo:errors)
- Persisters allowlist non-sensitive queries via `shouldDehydrateQuery` + a `buster`. (TQ:persist)

---

## TanStack Table

TanStack Table is headless — it emits row/cell/header objects and you supply the
markup. The dominant real risk is **rendering untrusted cell/header content as raw
HTML** (XSS); the rest are correctness/performance patterns that also cause DoS-like
render loops.

### Antipatterns → Best practices

| # | Antipattern | Corresponding best practice | Source (name + URL) | Statically detectable |
|---|---|---|---|---|
| 1 | Rendering a cell value as raw HTML via `dangerouslySetInnerHTML={{ __html: cell.getValue() }}` in a `cell`/`accessorFn` renderer — stored XSS if the value comes from user/API data. | Return the value as a text child (`{cell.getValue()}`), which React escapes; if HTML is truly required, sanitize with DOMPurify first. | OWASP:XSS (<https://cheatsheetseries.owasp.org/cheatsheets/Cross_Site_Scripting_Prevention_Cheat_Sheet.html>); TT:flexrender (<https://tanstack.com/table/latest/docs/framework/react/guide/flex-render>) | yes (`dangerouslySetInnerHTML` inside a column `cell`/`header`/`footer` renderer) |
| 2 | In non-React adapters (Vanilla/Lit/Angular), building cell markup with `el.innerHTML = cell.getValue()` / template interpolation of untrusted values. | Use `textContent`/`innerText`, or the adapter's escaped binding; sanitize before any `innerHTML`. | OWASP:DOMXSS (<https://cheatsheetseries.owasp.org/cheatsheets/DOM_based_XSS_Prevention_Cheat_Sheet.html>); TT:flexrender | yes (`.innerHTML =` fed by `getValue()`/row data in table cell code) |
| 3 | Rendering a header/column label (`header: userProvidedLabel`) as HTML from user-configurable report definitions. | Escape header labels; treat column config from users as untrusted. | OWASP:XSS; TT:flexrender | partial (HTML sinks in `header` renderers) |
| 4 | Bypassing `flexRender` and calling a column's `cell` function output through an unescaped HTML sink. | Render header/cell/footer through `flexRender`, which returns React nodes that React escapes. | TT:flexrender | partial |
| 5 | Passing an inline `data={[...]}`/`data={rows.filter(...)}` array to the table — a new reference every render forces every row/cell to rebuild and can trigger an auto-reset infinite render loop. | Give `data` a stable reference: `useState`/`useMemo`/query cache; memoize transforms with `useMemo`. | TT:data (<https://tanstack.com/table/latest/docs/guide/data>) | yes (`data={[` or `data={...filter/map/...}` inline at the `useReactTable`/`useTable` call) |
| 6 | Defining `columns` inline in the component body (`const columns = [...]`) — new reference every render rebuilds column/header structures. | Define `columns` with `useMemo(() => [...], [])` or outside the component. | TT:data | yes (`columns` array literal declared in component scope without `useMemo`) |
| 7 | Transforming data inline in the table options (`data: data.filter(...)`) even when the source is stable — the filter creates a new array each render. | `useMemo` the transformation so the reference only changes when inputs change. | TT:data | yes (`.filter(`/`.map(`/`.sort(` directly in the `data:` option) |
| 8 | Recreating the `features`/`tableFeatures({})` object in render (v9) — re-applied to the table every render. | Define `features` once outside the component. | TT:data | yes (`tableFeatures(` called inside component body) |
| 9 | Enabling client-side row models (`getFilteredRowModel`, `getSortedRowModel`, `getPaginationRowModel`) and shipping the **entire** dataset to the browser — leaks rows the user shouldn't see and blows up memory. | Use server-side pagination/filtering/sorting (`manualPagination`/`manualFiltering`/`manualSorting`) and enforce authorization on the server. | TT:serverside (<https://tanstack.com/table/latest/docs/guide/client-side-vs-server-side>); OWASP:A01 (<https://owasp.org/Top10/A01_2021-Broken_Access_Control/>) | partial (client row models enabled on large/sensitive data; `manual*` flags absent) |
| 10 | Relying on client-side column visibility / filtering to "hide" sensitive columns while still sending the values in `data` — the data is in the DOM/JS and trivially inspectable. | Never send fields the user isn't authorized to see; strip them server-side before they reach `data`. | TT:serverside; OWASP:A01 | no (requires knowing the data payload; runtime/config) |
| 11 | Rendering thousands of rows without virtualization, freezing the tab (effective client-side DoS on low-end devices). | Virtualize large tables with TanStack Virtual; or paginate server-side. | TT:virtual (<https://tanstack.com/table/latest/docs/framework/react/guide/virtualization>); TT:serverside | partial (large client `data` + full render, no virtualizer) |
| 12 | Fully uncontrolled state when the app also mutates state elsewhere, or partially controlling state (`state={{ sorting }}`) **without** the matching `onSortingChange` — state freezes and can't update. | If you control a piece of state, always pair it with its `on*Change` handler; otherwise leave it uncontrolled. | TT:state (<https://tanstack.com/table/latest/docs/framework/react/guide/table-state>) | yes (`state={{ sorting }}` present but `onSortingChange` absent — same for filtering/pagination/rowSelection/columnVisibility) |
| 13 | Setting `initialState` **and** a controlled `state` for the same slice — the controlled value wins and the `initialState` is silently ignored, confusing behavior. | Use `initialState` for uncontrolled defaults only; don't duplicate a slice in both. | TT:state | yes (same state key appears in both `initialState` and `state`) |
| 14 | Using an `accessorKey` with a literal dot in the key name (e.g. a field literally named `"a.b"`) — the dot is parsed as a deep path and reads the wrong/undefined value. | Use an `accessorFn` to read keys that contain literal periods. | TT:data | partial (`accessorKey` string containing `.` — needs data shape to confirm) |
| 15 | Trusting `globalFilter`/`columnFilters` values as safe when echoing them back into markup (e.g. "results for `<filter>`") — reflected XSS of the filter string. | Render filter echoes as escaped text, never via an HTML sink. | OWASP:XSS | partial (filter value flowing into `dangerouslySetInnerHTML`/`innerHTML`) |

### Best-practice checklist (positive form, condensed)

- Cell/header/footer rendered via `flexRender` and as escaped React text; DOMPurify before any `dangerouslySetInnerHTML`. (TT:flexrender, OWASP:XSS)
- `data` and `columns` have stable references (`useMemo`/`useState`/module scope); transforms memoized. (TT:data)
- Server-side `manualPagination`/`manualFiltering`/`manualSorting` for large or access-controlled datasets; authorization enforced server-side. (TT:serverside, OWASP:A01)
- Sensitive fields stripped server-side, never merely hidden client-side. (OWASP:A01)
- Large tables virtualized. (TT:virtual)
- Every controlled state slice paired with its `on*Change`; no slice in both `initialState` and `state`. (TT:state)
- `accessorFn` for keys containing literal dots. (TT:data)

---

## Notes for keur rule authors

- **Highest-signal static rules (low false-positive):** `dangerouslySetInnerHTML` in a
  Table cell/header renderer (TT #1), `new QueryClient()` in a component body (TQ #21),
  inline `data={[...]}`/`columns` literal at the table call (TT #5/#6), controlled
  state slice without its `on*Change` (TT #12), `retry: true` (TQ #11),
  `persistQueryClient` with `window.localStorage` and no dehydrate allowlist (TQ #1),
  and `refetchIntervalInBackground: true` (TQ #30).
- **Partial rules** need light dataflow (e.g. a `queryFn` variable missing from the
  `queryKey`) — this is exactly what `@tanstack/eslint-plugin-query`'s `exhaustive-deps`
  already does, so those patterns are known-detectable in principle.
- **Runtime/config-only** (not for a source rule): whether persisted/cached data is
  actually sensitive, whether the server strips unauthorized fields, and background-error
  UX. Flag these in docs/checklists rather than as blocking rules.
- The TanStack libraries themselves ship no security config; the rules target
  *application wiring*, which matches keur's staged-file, pattern-based model.
