# dockerfile-root.tape — keur catches a container running as root (ADR-047).
# @shows KEUR-IAC-002
# @explains ADR-036
#
# The "my linter can't do that" moment: ESLint/flake8 never look at your
# Dockerfile. keur flags a container with no USER (runs as root -> container
# escape = host root). Loop: scan warns -> keur learn -> add USER -> clean.
#
# Hermetic + PII-free: neutral /tmp HOME/XDG, keur from PATH (rules relative to
# the binary, ADR-063). Files written via heredoc so there's no escaping.

Output dockerfile-root.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1020
Set Height 720
Set FontSize 14
Set Padding 20
Set TypingSpeed 42ms

# --- Arrange (hidden): neutral isolated env + disposable repo ---------------
Hide
Type "export HOME=/tmp/demo-home XDG_CONFIG_HOME=/tmp/demo-home/.config XDG_CACHE_HOME=/tmp/demo-home/.cache"
Enter
Type "rm -rf /tmp/demo-home /tmp/demo && mkdir -p /tmp/demo-home/.config /tmp/demo && cd /tmp/demo"
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
Type "clear"
Enter
Show

Type "# Arrange: a fresh repo — about to add a Dockerfile your JS linter ignores"
Enter
Sleep 1400ms

# --- Act 1: a Dockerfile with no USER, scan it ------------------------------
Type "# Act 1: a normal-looking Dockerfile — no USER instruction"
Enter
Sleep 1s
Type "cat > Dockerfile <<'EOF'"
Enter
Type "FROM node:20-alpine"
Enter
Type "WORKDIR /app"
Enter
Type "COPY . ."
Enter
Type "RUN npm ci"
Enter
Type 'CMD ["node","server.js"]'
Enter
Type "EOF"
Enter
Sleep 600ms
Type "keur scan Dockerfile | grep IAC-002"
Enter
Sleep 3000ms

# --- Act 2: ask keur why that matters ---------------------------------------
Type "# Act 2: why does keur care? ask it"
Enter
Sleep 1200ms
Type "keur learn KEUR-IAC-002"
Enter
Sleep 3500ms

# --- Act 3: add a non-root USER, as advised ---------------------------------
Type "# Act 3: add a non-root USER, as keur advised"
Enter
Sleep 1200ms
Type "cat > Dockerfile <<'EOF'"
Enter
Type "FROM node:20-alpine"
Enter
Type "WORKDIR /app"
Enter
Type "COPY . ."
Enter
Type "RUN npm ci"
Enter
Type "USER node"
Enter
Type 'CMD ["node","server.js"]'
Enter
Type "EOF"
Enter
Sleep 600ms
Type "keur scan Dockerfile | grep -c IAC-002"
Enter
Sleep 2500ms

# --- Assert: no longer flagged ----------------------------------------------
Type "# Assert: 0 — the container no longer runs as root"
Enter
Sleep 2800ms
