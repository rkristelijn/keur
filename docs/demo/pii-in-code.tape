# pii-in-code.tape — keur flags PII (email, phone, IP) in source (ADR-047).
# @shows SEC-013
# @explains ADR-047
#
# Native PII detection: SEC-013 is a built-in repo rule — it warns on email /
# phone / IP literals so they don't get committed. vs a DIY grep: one rule
# covers all three with a fix hint, inside the PASS/WARN/BLOCK policy.
#
# Hermetic + PII-free: the Arrange sets HOME/XDG to a neutral /tmp path (no
# username, no machine config/vault leak) and runs keur from PATH (keur finds
# its rules relative to its own binary, ADR-063 — no repo path needed).

Output pii-in-code.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1000
Set Height 600
Set FontSize 14
Set Padding 20
Set TypingSpeed 45ms

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

Type "# Arrange: a fresh repo (keur runs isolated — no machine config)"
Enter
Sleep 1200ms

# --- Act: write a config with a real-looking email, phone, IP, then scan ----
Type "# Act: write a config with a real-looking email, phone and IP"
Enter
Sleep 1s
Type "cat > config.py <<'EOF'"
Enter
Type "support_email = 'jan.devries@acme.com'"
Enter
Type "hotline       = '040-123-4567'"
Enter
Type "origin_ip     = '3.64.108.185'"
Enter
Type "EOF"
Enter
Sleep 600ms
Type "keur scan config.py | grep SEC-013"
Enter
Sleep 3000ms

# --- Assert: three SEC-013 warnings, one per PII type -----------------------
Type "# Assert: SEC-013 flags each PII literal — email, phone, IP."
Enter
Sleep 2800ms
