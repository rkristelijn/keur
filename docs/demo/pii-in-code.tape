# pii-in-code.tape — keur flags PII (email, phone, IP) in source (ADR-047).
# @shows SEC-013
# @explains ADR-047
#
# Native PII detection: SEC-013 is a built-in repo rule (rules/security/), not a
# custom list — it warns on email / phone / IP literals so they don't get
# committed. vs a DIY grep: one rule covers all three, carries a fix hint, plugs
# into the same PASS/WARN/BLOCK policy and the keur:ignore allowlist (ADR-046).
#
# Runs through docs/demo/sandbox.sh so the recording is HERMETIC (no personal
# vault / global config leaks in — see sandbox.md). Uses `keur check` on a staged
# file so only the PII finding shows, not repo-hygiene noise.

Output pii-in-code.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1000
Set Height 600
Set FontSize 14
Set Padding 20
Set TypingSpeed 45ms

# --- Arrange (hidden): clean neutral shell, disposable repo in /tmp ----------
# SB points at the sandbox so every keur call is hermetic. Disposable repo in
# /tmp keeps the username out of paths and sits outside any repo's .keur/rules.
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

Type "# Arrange: a fresh repo, keur running hermetically via the demo sandbox"
Enter
Sleep 1200ms

# --- Act: write a config with real-looking email, phone, IP, then scan it ---
# Scan the single file (not the repo) so the demo stays focused on PII and
# doesn't surface repo-hygiene findings (README/LICENSE/config). Piped through
# a compact filter so only the SEC-013 verdict lines show.
Type "# Act: write a config with a real-looking email, phone and IP"
Enter
Sleep 1s
Type `printf 'support_email = "jan.devries@acme.com"\nhotline       = "040-123-4567"\norigin_ip     = "3.64.108.185"\n' > config.py`
Enter
Sleep 600ms
Type "# scan just this file, hermetically, via the demo sandbox"
Enter
Sleep 600ms
Type `$SB keur scan config.py | grep SEC-013`
Enter
Sleep 3000ms

# --- Assert: three SEC-013 warnings, one per PII type -----------------------
Type "# Assert: SEC-013 flags each PII literal — email, phone, IP. Use placeholders / env vars."
Enter
Sleep 2800ms
