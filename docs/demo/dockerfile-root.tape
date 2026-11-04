# dockerfile-root.tape — keur catches a container running as root (ADR-047).
# @shows KEUR-IAC-002
# @explains ADR-036
#
# The "my linter can't do that" moment: ESLint/Prettier/flake8 never look at
# your Dockerfile. keur flags a container with no USER (runs as root → container
# escape = host root, Dirty Pipe CVE-2022-0847). Loop: scan warns -> `keur learn`
# explains the risk + fix -> add `USER node` -> clean. This rule is WARN (a
# finding, not a hard block), so the demo shows the WARN→clean transition.
#
# Hermetic via sandbox.sh (native rule, no custom ruleset needed).

Output dockerfile-root.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1020
Set Height 720
Set FontSize 14
Set Padding 20
Set TypingSpeed 42ms

# --- Arrange (hidden): clean repo in /tmp, hermetic sandbox ------------------
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
Type "rm -rf /tmp/demo && mkdir -p /tmp/demo && cd /tmp/demo"
Enter
Type "git init -q && git config color.ui always"
Enter
Type "clear"
Enter
Show

Type "# Arrange: a fresh repo — about to add a Dockerfile your JS linter ignores"
Enter
Sleep 1400ms

# --- Act 1: write a Dockerfile with no USER, scan it ------------------------
Type "# Act 1: a normal-looking Dockerfile — no USER instruction"
Enter
Sleep 1s
Type `printf 'FROM node:20-alpine\nWORKDIR /app\nCOPY . .\nRUN npm ci\nCMD ["node","server.js"]\n' > Dockerfile`
Enter
Sleep 500ms
Type "$SB keur scan Dockerfile | grep IAC-002"
Enter
Sleep 3000ms

# --- Act 2: ask keur why that matters ---------------------------------------
Type "# Act 2: why does keur care? ask it"
Enter
Sleep 1200ms
Type "$SB keur learn KEUR-IAC-002"
Enter
Sleep 3500ms

# --- Act 3: add a non-root USER, as advised ---------------------------------
Type "# Act 3: add a non-root USER, as keur advised"
Enter
Sleep 1200ms
Type `printf 'FROM node:20-alpine\nWORKDIR /app\nCOPY . .\nRUN npm ci\nUSER node\nCMD ["node","server.js"]\n' > Dockerfile`
Enter
Sleep 500ms
Type "$SB keur scan Dockerfile | grep -c IAC-002"
Enter
Sleep 2500ms

# --- Assert: no longer flagged ----------------------------------------------
Type "# Assert: 0 — the container no longer runs as root"
Enter
Sleep 2500ms
