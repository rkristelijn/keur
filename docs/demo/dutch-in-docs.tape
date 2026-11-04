# dutch-in-docs.tape — a CUSTOM rule your linter can't do, end to end (ADR-047).
# @shows DEMO-NL-001
# @explains ADR-036
#
# The full loop: commit BLOCKED by a project rule -> keur learn explains ->
# fix -> passes. DEMO-NL-001 (English-only docs) is a CUSTOM rule — something
# ESLint/Prettier can't express. ~10 lines of .rule, same gate + explainer as
# the built-ins.
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
Type "export HOME=/tmp/demo-home XDG_CONFIG_HOME=/tmp/demo-home/.config XDG_CACHE_HOME=/tmp/demo-home/.cache"
Enter
Type "rm -rf /tmp/demo-home /tmp/demo && mkdir -p /tmp/demo-home/.config /tmp/demo/.keur/rules && cd /tmp/demo"
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
Type "cat > .keur/rules/DEMO-NL-001.rule <<'EOF'"
Enter
Type "id: DEMO-NL-001"
Enter
Type "title: Dutch-language text in documentation (English-only house rule)"
Enter
Type "category: quality"
Enter
Type "severity: warning"
Enter
Type "engine: pattern"
Enter
Type "action: block"
Enter
Type "confidence: 0.9"
Enter
Type "target:"
Enter
Type "  extensions: .md"
Enter
Type "patterns:"
Enter
Type "  - regex: (?i)\\b(het|een|niet|werkt|deze|zijn|voor|met|gebruik|volgende)\\b"
Enter
Type "    message: Dutch text in a doc — this repo's docs are English-only."
Enter
Type "fix: Write documentation in English."
Enter
Type "EOF"
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
Type "keur scan README.md | grep DEMO-NL-001"
Enter
Sleep 3000ms

# --- Act 2: ask keur what the rule is and how to fix it ---------------------
Type "# Act 2: blocked. ask keur what this rule is and how to fix it"
Enter
Sleep 1200ms
Type "keur learn DEMO-NL-001"
Enter
Sleep 3200ms

# --- Act 3: translate to English, scan again --------------------------------
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
Type "keur scan README.md | grep -c DEMO-NL-001"
Enter
Sleep 2500ms

# --- Assert: 0 — rule satisfied, the doc is English now ---------------------
Type "# Assert: 0 — rule satisfied, no longer blocked"
Enter
Sleep 2800ms
