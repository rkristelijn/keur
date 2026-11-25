# dutch-in-docs.tape — a CUSTOM rule your linter can't do, end to end (ADR-047).
# @shows DEMO-NL-001
# @explains ADR-036
#
# The full loop: commit BLOCKED by a project rule -> keur learn explains ->
# fix -> passes. DEMO-NL-001 (English-only docs) is a CUSTOM rule — something
# ESLint/Prettier can't express. ~10 lines of .rule, same gate + explainer as
# the built-ins.
#
# The rule is copied from docs/demo/rules/ (one source). A typed copy of it
# drifted once and silently never matched.
#
# The hidden setup pins a minimal .config/keur.toml ([policy] level = 1) so the
# repo-hygiene rules of a bare demo repo (no LICENSE, SECURITY.md, ...) do not
# decide the verdict: only the house rule does, BLOCK then PASS.
#
# Hermetic + PII-free: neutral /tmp HOME/XDG, keur from PATH. The custom rule is
# copied into the demo's own .keur/rules (a path keur discovers by walking up
# from the repo — ADR), so no machine path is typed.

Output dutch-in-docs.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1020
Set Height 760
Set FontSize 14
Set Padding 20
Set TypingSpeed 42ms

# --- Arrange (hidden): neutral env, repo, and the custom rule in .keur/rules -
Hide
Type "unset KEUR_RULES_DIRS"
Enter
Type `R="$(git rev-parse --show-toplevel)"`
Enter
Type "export HOME=/tmp/demo-dutch-in-docs-home XDG_CONFIG_HOME=/tmp/demo-dutch-in-docs-home/.config XDG_CACHE_HOME=/tmp/demo-dutch-in-docs-home/.cache"
Enter
Type "rm -rf /tmp/demo-dutch-in-docs-home /tmp/demo-dutch-in-docs && mkdir -p /tmp/demo-dutch-in-docs-home/.config /tmp/demo-dutch-in-docs/.keur/rules && cd /tmp/demo-dutch-in-docs"
Enter
Type "setopt interactive_comments"
Enter
Type "source /opt/homebrew/share/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh"
Enter
Type "export CLICOLOR_FORCE=1 CLICOLOR=1"
Enter
Type "PROMPT='%F{green}❯%f '"
Enter
Type "git init -q && git config color.ui always"
Enter
Type "mkdir -p .config && cat > .config/keur.toml <<'EOF'"
Enter
Type "[policy]"
Enter
Type "level = 1"
Enter
Type "EOF"
Enter
Type "git add .config"
Enter
Type `cp "$R"/docs/demo/rules/DEMO-NL-001-dutch-in-docs.rule .keur/rules/`
Enter
Type "clear"
Enter
Show

Type "# Arrange: a repo with a house rule — docs must be English (custom DEMO-NL-001)"
Enter
Sleep 1400ms

# --- Act 1: write a README in Dutch and try to commit -> BLOCK --------------
Type "# Act 1: write the README in Dutch and try to commit"
Enter
Sleep 1s
Type "cat > README.md <<'EOF'"
Enter
Type "# Project"
Enter
Type "Deze tool werkt niet zonder een config. Gebruik de volgende stappen."
Enter
Type "EOF"
Enter
Sleep 500ms
Type "git add README.md && keur check"
Enter
Wait@30s /❯\s*$/
Sleep 2500ms

# --- Act 2: ask keur what the rule is and how to fix it ---------------------
Type "# Act 2: blocked. ask keur what this rule is and how to fix it"
Enter
Sleep 1200ms
Type "keur learn DEMO-NL-001"
Enter
Sleep 3200ms

# --- Act 3: translate to English, run the gate again -----------------------
Type "# Act 3: translate the prose to English, as keur advised"
Enter
Sleep 1200ms
Type "cat > README.md <<'EOF'"
Enter
Type "# Project"
Enter
Type "This tool needs a config file. Follow the steps below."
Enter
Type "EOF"
Enter
Sleep 500ms
Type "git add README.md && keur check"
Enter
Wait@30s /❯\s*$/
Sleep 2000ms

# --- Assert: PASS — rule satisfied, the commit can go through ---------------
Type "# Assert: PASS — rule satisfied, no longer blocked"
Enter
Sleep 2800ms
