# dutch-in-docs.tape — a CUSTOM rule your linter can't do, end to end (ADR-047).
# @shows DEMO-NL-001
# @explains ADR-036
#
# The full loop a dev actually hits: commit is BLOCKED by a project rule ->
# `keur learn` explains why + how -> fix it -> commit passes. The rule
# (DEMO-NL-001, docs/demo/rules/) is CUSTOM — English-only docs, something
# ESLint/Prettier/flake8 cannot enforce. Shows keur is extensible: ~10 lines of
# .rule plug into the same gate + explainer as the built-ins.
#
# Hermetic via sandbox.sh; the sandbox passes KEUR_RULES_DIRS through so the
# custom rule loads without touching the machine.

Output dutch-in-docs.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1020
Set Height 760
Set FontSize 14
Set Padding 20
Set TypingSpeed 42ms

# --- Arrange (hidden): clean repo in /tmp, sandbox wired to the custom rule ---
Hide
Type "setopt interactive_comments"
Enter
Type "source /opt/homebrew/share/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh"
Enter
Type "export CLICOLOR_FORCE=1 CLICOLOR=1"
Enter
Type "PROMPT='%F{green}❯%f '"
Enter
Type "SB=$HOME/git/hub/keur-sonar/docs/demo/sandbox.sh"
Enter
Type "export KEUR_RULES_DIRS=$HOME/git/hub/keur-sonar/docs/demo/rules"
Enter
Type "rm -rf /tmp/demo && mkdir -p /tmp/demo && cd /tmp/demo"
Enter
Type "git init -q && git config color.ui always"
Enter
# minimal keur.toml so the ONLY blocker is our custom rule, not repo hygiene
Type "mkdir -p .config && printf '[difficulty]\\nlevel = 2\\n[policy]\\nblock-at = 0.9\\nwarn-at = 0.5\\n' > .config/keur.toml"
Enter
Type "clear"
Enter
Show

Type "# Arrange: a repo with a house rule — docs must be English (custom DEMO-NL-001)"
Enter
Sleep 1400ms

# --- Act 1: write a README in Dutch and try to commit -> BLOCK ---------------
Type "# Act 1: write the README in Dutch and try to commit"
Enter
Sleep 1s
Type `printf '# Project\n\nDeze tool werkt niet zonder een config. Gebruik de volgende stappen.\n' > README.md`
Enter
Sleep 500ms
Type "git add README.md"
Enter
Sleep 400ms
Type "$SB keur check | grep -E 'BLOCK|DEMO-NL-001'"
Enter
Sleep 3000ms

# --- Act 2: ask keur WHY and what to do --------------------------------------
Type "# Act 2: blocked. Ask keur what this rule is and how to fix it"
Enter
Sleep 1200ms
Type "$SB keur learn DEMO-NL-001"
Enter
Sleep 3500ms

# --- Act 3: do the fix (translate to English), then commit -> PASS -----------
Type "# Act 3: translate the prose to English, as keur advised"
Enter
Sleep 1200ms
Type `printf '# Project\n\nThis tool needs a config file. Follow the steps below.\n' > README.md`
Enter
Sleep 500ms
Type "git add README.md"
Enter
Sleep 400ms
Type "$SB keur check | tail -1"
Enter
Sleep 2500ms

# --- Assert: fixed, the commit now goes through ------------------------------
Type "# Assert: rule satisfied — the commit is no longer blocked"
Enter
Sleep 2500ms
