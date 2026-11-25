# Tape authoring learnings — gotchas when writing keur demo tapes

Hard-won lessons from building the demo tapes (VHS `.tape` → GIF, ADR-047).
Each one is a real failure we hit and verified, not theory. Read this before
adding or editing a tape; it will save you the debugging we already did.

## 1. Multi-line files: use a heredoc, never `printf '...\n...'`

`printf 'a\nb\n' > file` is unreadable AND renders the `\n` as literal text in
the GIF (it runs fine in a shell, so `run.sh` does not catch it — only the GIF
is broken). Use a heredoc: real lines, no escapes, readable on screen.

```text
Type "cat > config.js <<'EOF'"
Enter
Type "line one"
Enter
Type "line two"
Enter
Type "EOF"
Enter
```

Quote the delimiter (`<<'EOF'`) so `$VAR` inside the body is NOT expanded —
keeps `$SB`, `$HOME`, etc. literal and avoids surprises.

## 2. zsh-syntax-highlighting BREAKS heredocs with shell metachars in the body

**The subtle one.** If `zsh-syntax-highlighting` is sourced, typing a heredoc
whose body contains `( ) ' { } | ?` corrupts the input mid-heredoc — the file is
not created, and the next command shows `No such file or directory`.

Verified with an A/B test (same heredoc, only difference = highlighting):

- WITH highlighting → file NOT created.
- WITHOUT → file created fine.

This is why `dockerfile-root` works (body `FROM/WORKDIR/COPY/RUN`, no metachars)
but `ai-slop-fake-test` (`test('...', () => {`) and `dutch-in-docs` (regex
`(?i)\b(het|...)`) fail. It is NOT the heredoc and NOT the `\n` — it is the
highlighter parsing over the heredoc input.

## 3. Preferred pattern: pre-stage a fixture, show it with `cat`

Don't type a code/regex file live at all. Keep it as a committed fixture and
`cat` it — `cat` output is NOT touched by the input highlighter, so it renders
clean with highlighting on, and there is no heredoc to break.

```text
# hidden Arrange: copy the fixture in before anything is shown
Hide
Type "cp fixtures/config.test.js ."
Enter
Show
# visible: show what the file contains, then run keur on it
Type "cat config.test.js"
Enter
Type "keur scan config.test.js | grep SLOP-107"
Enter
```

Reproducible (the fixture is versioned), no live-creation risk, and the viewer
still sees the file + the verdict. Use `cat`, not `bat` — no extra dependency,
no box-drawing frame around the output.

## 4. Comments need `setopt interactive_comments`

A `# ...` line typed at an interactive zsh prompt without this setopt runs as a
command → `zsh: command not found: #`. Set it once in the hidden Arrange:

```text
Type "setopt interactive_comments"
Enter
```

## 5. `Output` takes a RELATIVE path, not an absolute one

`Output /tmp/foo.txt` fails — VHS tokenises on `/` (`Invalid command: tmp`).
Use a relative name in the working dir: `Output foo.txt`.

## 6. Golden output comes from `run.sh`, not from vhs

vhs is a demo recorder, not a test harness. Its `Output <name>.txt` is a
frame-by-frame animation dump — fragile to trim into a snapshot, and nobody uses
it for golden tests. Generate `<name>.out.txt` from `run.sh` (the tape's
commands in a shell) via `gen-golden.sh`. The one thing run.sh misses is a pure
*render* bug (see #1/#2) — fix that at the source (heredoc / fixture), not by
sourcing golden output from vhs.

## 7. No PII: never type `$HOME` or an absolute home path

The shell expands `$HOME` to `/Users/<name>` (or `/home/<name>`) in the GIF —
your username burned into a committed binary. Use relative paths, `/tmp/demo-<name>`,
or a throwaway `HOME` (see `sandbox.sh`). `check-pii.sh` gates this; run it
before committing a tape or a golden output.

## 8. Isolate HOME / XDG for reproducibility

A demo runs on a maintainer's machine. Without isolation, `keur version` reads
the renderer's global `~/.config/keur/keur.toml` (which may be an old version —
we hit a `0.1.0` leak while the binary was `0.2.0`), and the GIF differs per
machine. In the hidden Arrange point HOME/XDG at throwaway temp dirs (or use
`sandbox.sh`, which does exactly this).

## 9. One working directory per tape: `/tmp/demo-<name>`

Every tape starts with `rm -rf <dir> && mkdir -p <dir> && cd <dir>`. When all
tapes shared `/tmp/demo`, rendering two of them close together let one delete
the other's working directory mid-recording: the GIF then showed
`zsh: no such file or directory` and no verdict at all. Each tape owns
`/tmp/demo-<name>` (and `-home`, `-fixtures` suffixes); `gen-golden.sh` fails a
tape that uses any other `/tmp/demo*` path.

## 10. Copy a rule file, do not type it

A rule typed into a quoted heredoc keeps its backslashes literally, so a typed
`\\b` never matched and the demo silently showed no BLOCK. Copy the rule from
`docs/demo/rules/` instead (one source, covered by `tests/e2e/test_demo_rules.sh`).
Also `unset KEUR_RULES_DIRS` in the hidden setup so a renderer's own rule
directories cannot change the findings.

## 11. Show the gate, not only the scanner

`keur scan` reports findings and always exits 0; it never decides BLOCK or PASS.
A tape that claims "blocked" must stage the file and run `keur check`. Pin a
minimal `.config/keur.toml` (`[policy] level = 1`) so the repo-hygiene rules of
a bare demo repo do not decide the verdict.

## Quick checklist before committing a tape

- [ ] Multi-line content via heredoc or a pre-staged fixture + `cat` (not printf)
- [ ] No `( ) { } | ?` typed into a live heredoc while highlighting is on
- [ ] `setopt interactive_comments` set before any `#` comment line
- [ ] `Output <name>.gif` uses a relative path
- [ ] No `$HOME` / `/Users/` / `/home/` typed — `./check-pii.sh <tape>` is clean
- [ ] HOME/XDG isolated (or via `sandbox.sh`), `KEUR_RULES_DIRS` unset
- [ ] Own working dir `/tmp/demo-<name>`; rule files copied, not typed
- [ ] A "blocked" claim runs `keur check` on staged files, not only `keur scan`
- [ ] `./gen-golden.sh <tape>` regenerated, `./run.sh <tape>` shows the Assert
- [ ] `@shows <id>` marker present (ADR-047)
