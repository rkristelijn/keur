# JavaScript / TypeScript Top 50 Antipatterns

Research feeding keur rule development. Scope: **language-level** JS/TS
antipatterns (not framework-specific — those live in `nestjs.md`, `nextjs.md`,
etc.). Each row pairs a concrete antipattern with its corresponding best
practice, cites an authoritative source, and marks whether it is **statically
detectable** in source (so keur could write a `.rule`).

Legend for "statically detectable":

- **yes** — a regex/AST rule over source can reliably flag it.
- **partial** — a rule can flag the suspicious surface but cannot decide
  correctness; heuristic / warn-level only.
- **no** — needs cross-file dataflow or runtime; out of scope for a static rule.

Sources are canonical: MDN, the OWASP Cheat Sheet Series / Top 10, the
TypeScript Handbook, typescript-eslint & ESLint rule docs, the Node.js Security
Best Practices, and Google/Airbnb style guides. Concrete API names are used
throughout. Nothing here is invented; every row traces to a cited page.

Grouped: security (1-14), correctness/type-safety (15-30), async/error handling
(31-40), performance (41-46), maintainability/slop (47-50).

---

## Antipatterns → Best practices

| # | antipattern | corresponding best practice | source (name + URL) | statically detectable |
|---|-------------|-----------------------------|---------------------|-----------------------|
| 1 | `eval(userInput)` / `new Function(userInput)` — arbitrary code execution | Never eval untrusted input; use `JSON.parse`, a lookup map, or a safe expression parser | OWASP — Code Injection (<https://owasp.org/www-community/attacks/Code_Injection>) ; MDN `eval` "never use eval" (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/eval>) | yes |
| 2 | `child_process.exec('cmd ' + input)` — OS command injection | Use `execFile`/`spawn` with an argument array; validate input | OWASP — Command Injection (<https://owasp.org/www-community/attacks/Command_Injection>) ; Node.js Security Best Practices (<https://nodejs.org/en/learn/getting-started/security-best-practices>) | yes |
| 3 | `element.innerHTML = userInput` — DOM XSS | Use `textContent`, or sanitize with DOMPurify before assigning HTML | OWASP — DOM XSS Prevention CS (<https://cheatsheetseries.owasp.org/cheatsheets/DOM_based_XSS_Prevention_Cheat_Sheet.html>) | yes |
| 4 | `document.write(userInput)` — XSS + blocks parser | Build DOM nodes / use templating with escaping | MDN `document.write` (discouraged) (<https://developer.mozilla.org/en-US/docs/Web/API/Document/write>) | yes |
| 5 | `JSON.parse(untrusted)` with no try/catch — throws on malformed input, DoS | Wrap in try/catch; validate shape with a schema (zod/ajv) | MDN `JSON.parse` (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/JSON/parse>) ; OWASP Deserialization CS (<https://cheatsheetseries.owasp.org/cheatsheets/Deserialization_Cheat_Sheet.html>) | partial |
| 6 | `Object.assign(target, req.body)` / spread of untrusted body — prototype pollution & mass assignment | Whitelist fields explicitly; `Object.create(null)`; guard `__proto__` | OWASP — Prototype Pollution (<https://owasp.org/www-community/attacks/Prototype_pollution>) ; Mass Assignment CS (<https://cheatsheetseries.owasp.org/cheatsheets/Mass_Assignment_Cheat_Sheet.html>) | partial |
| 7 | Assigning to `obj['__proto__']` / `obj.constructor.prototype` from input | Reject `__proto__`/`constructor`/`prototype` keys; use `Map` | OWASP Prototype Pollution (<https://owasp.org/www-community/attacks/Prototype_pollution>) | yes |
| 8 | `Math.random()` for tokens / IDs / secrets — predictable | `crypto.randomUUID()` / `crypto.getRandomValues()` | MDN `Crypto.getRandomValues` (<https://developer.mozilla.org/en-US/docs/Web/API/Crypto/getRandomValues>) ; OWASP Cryptographic Storage CS | yes |
| 9 | `require(variable)` / dynamic `import(userInput)` — arbitrary module load | Static imports, or an allow-list map of permitted modules | Node.js Security Best Practices (<https://nodejs.org/en/learn/getting-started/security-best-practices>) | yes |
| 10 | Regex built from user input (`new RegExp(input)`) — ReDoS / injection | Escape input, bound length, or avoid dynamic regex | OWASP — ReDoS (<https://owasp.org/www-community/attacks/Regular_expression_Denial_of_Service_-_ReDoS>) | yes |
| 11 | Catastrophic-backtracking regex literal (`(a+)+`, `(.*)*`) — ReDoS | Use linear patterns; avoid nested quantifiers / overlapping alternation | OWASP ReDoS (<https://owasp.org/www-community/attacks/Regular_expression_Denial_of_Service_-_ReDoS>) | yes |
| 12 | `fetch(url)` with no timeout / `AbortController` — hung connections exhaust resources | Use `AbortSignal.timeout(ms)` or an explicit AbortController | MDN `AbortSignal.timeout` (<https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/timeout_static>) ; OWASP Node.js CS | partial |
| 13 | `postMessage(data, '*')` — sends to any origin | Pass a specific `targetOrigin`; verify `event.origin` on receive | MDN `Window.postMessage` (security) (<https://developer.mozilla.org/en-US/docs/Web/API/Window/postMessage#security_concerns>) | yes |
| 14 | Secret / API key as a string literal in source | Load from env / secret store; never commit literals | OWASP Secrets Management CS (<https://cheatsheetseries.owasp.org/cheatsheets/Secrets_Management_Cheat_Sheet.html>) | yes |
| 15 | `==` / `!=` (loose equality) — type coercion surprises (`0 == ''`, `null == undefined`) | Use `===` / `!==` strict equality | MDN Equality comparisons (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Equality_comparisons_and_sameness>) ; ESLint `eqeqeq` | yes |
| 16 | `var` declarations — function-scoped, hoisting bugs | `const` by default, `let` when reassigned | MDN `let`/`const` (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/const>) ; ESLint `no-var` | yes |
| 17 | `any` type / `@ts-ignore` to silence errors — defeats the type system | Use precise types / `unknown` + narrowing; `@ts-expect-error` with reason | TypeScript Handbook — `unknown` (<https://www.typescriptlang.org/docs/handbook/2/functions.html>) ; typescript-eslint `no-explicit-any` | yes |
| 18 | Non-null assertion `foo!.bar` to bypass null checks | Narrow with a guard / optional chaining; handle the null case | typescript-eslint `no-non-null-assertion` (<https://typescript-eslint.io/rules/no-non-null-assertion/>) | yes |
| 19 | Type assertion `x as SomeType` on untrusted data — lies to the compiler | Validate at runtime (zod/io-ts) then the type is earned | TypeScript Handbook — Type Assertions (<https://www.typescriptlang.org/docs/handbook/2/everyday-types.html#type-assertions>) | partial |
| 20 | `parseInt(x)` without radix — legacy octal/hex surprises | `parseInt(x, 10)` or `Number(x)` | MDN `parseInt` (radix note) (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/parseInt>) ; ESLint `radix` | yes |
| 21 | `for...in` over an array — iterates keys + inherited props, wrong order | `for...of`, `.forEach`, or index loop | MDN `for...in` (arrays caveat) (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/for...in>) | partial |
| 22 | Mutating a function parameter / shared object | Treat params as immutable; copy before mutate | Airbnb JS Style Guide — no-param-reassign (<https://github.com/airbnb/javascript#functions--mutate-params>) | partial |
| 23 | Extending native prototypes (`Array.prototype.foo = ...`) | Use standalone functions / utilities; never patch built-ins | MDN — do not extend built-in prototypes (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Object/prototype>) | yes |
| 24 | `delete arr[i]` on an array — leaves a hole, length unchanged | `arr.splice(i, 1)` | MDN `delete` (arrays) (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/delete>) | yes |
| 25 | Floating-point money (`0.1 + 0.2 !== 0.3`) | Integer cents, or a decimal library | MDN Number precision (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Number>) | no |
| 26 | `switch` without `break` (unintended fall-through) | Add `break`/`return`; annotate deliberate fall-through | MDN `switch` (fall-through) (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/switch>) ; ESLint `no-fallthrough` | partial |
| 27 | `enum` in TS (runtime cost, non-tree-shakeable, string/number pitfalls) | `as const` union / object literal | TypeScript Handbook — Enums caveats (<https://www.typescriptlang.org/docs/handbook/enums.html>) | partial |
| 28 | Function with many positional boolean params (`fn(true, false, true)`) | Options object with named fields | Clean Code / Airbnb — arguments (<https://github.com/airbnb/javascript#functions--signature-invocation-indentation>) | no |
| 29 | Truthiness check on possibly-`0`/`''` (`if (count)`) drops valid falsy values | Compare explicitly (`if (count != null)`, `> 0`) | MDN Truthy/Falsy (<https://developer.mozilla.org/en-US/docs/Glossary/Truthy>) | partial |
| 30 | `String(x)` / `+x` implicit coercion instead of explicit intent | Use explicit `Number(x)`, `String(x)`, `Boolean(x)` | MDN Type coercion (<https://developer.mozilla.org/en-US/docs/Glossary/Type_coercion>) | no |
| 31 | Empty `catch {}` — swallows the error silently | Handle, rethrow, or log with context; never swallow | Node.js Best Practices — error handling (<https://github.com/goldbergyoni/nodebestpractices#2-error-handling-practices>) ; MDN `try...catch` | yes |
| 32 | `catch (e) {}` that logs but continues as if nothing happened | Decide: recover, rethrow, or fail — don't fake success | Node.js Best Practices — error handling (<https://github.com/goldbergyoni/nodebestpractices>) | partial |
| 33 | `.catch(() => {})` on a promise — swallowed rejection | Handle the rejection meaningfully or let it propagate | MDN Promise error handling (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Using_promises#error_handling>) | yes |
| 34 | `async` function whose promise is never awaited (floating promise) | `await` it, `void` it deliberately, or `.catch()` | typescript-eslint `no-floating-promises` (<https://typescript-eslint.io/rules/no-floating-promises/>) | partial |
| 35 | `await` inside a `for` loop over independent work — serial, slow | `Promise.all(items.map(...))` for concurrency | MDN `Promise.all` (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/all>) | partial |
| 36 | `new Promise` wrapping something already promise-returning (explicit-construction antipattern) | Return the existing promise / use async | MDN — avoid the Promise constructor antipattern (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Using_promises>) | partial |
| 37 | Unhandled `throw` of a non-Error (`throw 'string'`) — no stack trace | Always `throw new Error(...)` (or a subclass) | MDN `throw` (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/throw>) ; ESLint `no-throw-literal` | yes |
| 38 | Mixing callbacks and promises for the same flow | Pick one; promisify callback APIs (`util.promisify`) | Node.js docs `util.promisify` (<https://nodejs.org/api/util.html#utilpromisifyoriginal>) | no |
| 39 | Blocking the event loop with sync I/O (`fs.readFileSync`) in a request path | Use async `fs.promises`; keep sync only for startup | Node.js Best Practices — don't block the loop (<https://github.com/goldbergyoni/nodebestpractices>) | partial |
| 40 | `process.on('uncaughtException')` used to keep running after a crash | Log + exit; let a supervisor restart | Node.js docs — `uncaughtException` warning (<https://nodejs.org/api/process.html#event-uncaughtexception>) | partial |
| 41 | N+1 queries: awaiting a DB call inside a `.map`/loop per row | Batch / join / `Promise.all` / dataloader | Node.js Best Practices — performance (<https://github.com/goldbergyoni/nodebestpractices>) | partial |
| 42 | Building large strings with `+=` in a loop | `array.push(...)` then `join('')` | MDN String concatenation perf (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/String>) | partial |
| 43 | Recomputing / re-sorting inside a render or hot loop | Hoist the computation; memoize | MDN — performance (<https://developer.mozilla.org/en-US/docs/Learn/Performance>) | no |
| 44 | Unbounded in-memory cache / array that only grows (memory leak) | Bound size (LRU), evict, or use a TTL | Node.js Best Practices — memory (<https://github.com/goldbergyoni/nodebestpractices>) | no |
| 45 | `array.includes` / linear scan inside a loop over another array (O(n²)) | Build a `Set`/`Map` once, then O(1) lookups | MDN `Set` (<https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Set>) | partial |
| 46 | `JSON.parse(JSON.stringify(obj))` for deep clone — slow, drops types | `structuredClone(obj)` | MDN `structuredClone` (<https://developer.mozilla.org/en-US/docs/Web/API/structuredClone>) | yes |
| 47 | `addEventListener` / `emitter.on` / `subscribe` with no matching removal — listener leak | Pair with `removeEventListener`/`off`/`unsubscribe` (or `AbortSignal`) | MDN `EventTarget.addEventListener` (<https://developer.mozilla.org/en-US/docs/Web/API/EventTarget/addEventListener>) ; Node.js EventEmitter memory-leak warning (<https://nodejs.org/api/events.html#emittersetmaxlistenersn>) | partial |
| 48 | `setInterval` / `setTimeout` never cleared — timer leak | Keep the handle and `clearInterval`/`clearTimeout` on teardown | MDN `setInterval` (clear note) (<https://developer.mozilla.org/en-US/docs/Web/API/setInterval>) | partial |
| 49 | Opening a resource (file/db/socket) without closing on all paths | `try/finally` or `using` (explicit resource management) | TC39 Explicit Resource Management (`using`) (<https://github.com/tc39/proposal-explicit-resource-management>) | partial |
| 50 | `console.log` left in production / used as the logging strategy | Use a real logger (levels, structured), strip debug logs | Node.js Best Practices — logging (<https://github.com/goldbergyoni/nodebestpractices>) | yes |
