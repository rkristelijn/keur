# secret-in-code.tape — keur flags a hardcoded secret in source (ADR-047).
# @shows KEUR-SEC-002
# @explains ADR-047

Output secret-in-code.gif

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

# --- Act: hardcode a password in a source file and try to commit ------------
Type "# Act: hardcode a password in a Python file and try to commit"
Enter
Sleep 1s
Type `echo 'password = "s3cr3t-p4ssw0rd"' > config.py`
Enter
Sleep 500ms
Type "git add config.py"
Enter
Sleep 500ms
Type "git commit -m 'add config'"
Enter
Sleep 2800ms

# --- Assert: flagged, the secret belongs in a secret store ------------------
Type "# Assert: flagged, a hardcoded secret should come from a secret store"
Enter
Sleep 2500ms
