# commit-env-file.tape — keur blocks committing a .env file (ADR-047).
# @shows SEC-100 KEUR-SECRET-003
# @explains ADR-047

Output commit-env-file.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1000
Set Height 620
Set FontSize 14
Set Padding 20
Set TypingSpeed 45ms

# --- Arrange (hidden): clean neutral shell, disposable repo in /tmp ---------
Hide
Type "setopt interactive_comments"
Enter
Type "source /opt/homebrew/share/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh"
Enter
Type "export CLICOLOR_FORCE=1 CLICOLOR=1"
Enter
Type "PROMPT='%F{green}❯%f '"
Enter
Type "rm -rf /tmp/demo && mkdir -p /tmp/demo && cd /tmp/demo"
Enter
Type "git init -q && git config color.ui always"
Enter
Type "clear"
Enter
Show

Type "# Arrange: a fresh repo with the global keur pre-commit hook"
Enter
Sleep 1200ms

# --- Act: stage a .env file and try to commit it ----------------------------
Type "# Act: add a .env file and try to commit it"
Enter
Sleep 1s
Type "echo 'API_TOKEN=changeme' > .env"
Enter
Sleep 500ms
Type "git add .env"
Enter
Sleep 500ms
Type "git commit -m 'add config'"
Enter
Sleep 2800ms

# --- Assert: blocked, the .env never got committed --------------------------
Type "# Assert: blocked, committing a .env file is not allowed"
Enter
Sleep 2500ms
