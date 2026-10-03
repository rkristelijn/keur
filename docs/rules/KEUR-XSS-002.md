---
reviewed: false
---

# KEUR-XSS-002 — Tapestry @Insert raw="true" disables output escaping
category: xss · severity: warning · CWE-79

## Why this matters
Apache Tapestry's `@Insert` component HTML-escapes its output by default, which
prevents cross-site scripting. Adding `raw="true"` opts out of that escaping and
writes the bound value to the page verbatim. If any part of that value is
user- or tenant-influenced (help text, FAQ answers, news items, notes, contact
fields), an attacker can store markup that executes in another user's browser —
a stored XSS. A single attribute flips a safe render into an injection point.

## Unsafe (what fires)
```html
<span jwcid="@Insert" value="ognl:helpItem.helpText" raw="true" class="helpText"/>
```

## Safe (what does not)
```html
<!-- default escaping: Tapestry renders the value as text -->
<span jwcid="@Insert" value="ognl:helpItem.helpText" class="helpText"/>
```

## Fix
Remove `raw="true"` so Tapestry HTML-escapes the output. Only keep it for values
that are provably trusted (constant markup you control). For user-supplied rich
text that genuinely must render as HTML, sanitise it server-side (e.g. the OWASP
Java HTML Sanitizer) before it reaches the template.

## Suppress (if genuinely a false positive)
`# keur:ignore KEUR-XSS-002`  (or a `.keurignore` entry)

Legitimate uses exist (rendering pre-built, trusted markup). Suppress per-line
with a comment explaining why the bound value is trusted.

## References
- CWE-79: Improper Neutralization of Input During Web Page Generation
- OWASP: Cross Site Scripting Prevention Cheat Sheet
- Apache Tapestry `@Insert` component `raw` parameter
