# TEMPLATE.tape — starting point for a keur feature demo (ADR-047).
#
# Copy this to docs/demo/<feature>.tape and fill in the three AAA phases.
# Each demo demonstrates ONE feature, Arrange / Act / Assert, like a test.
#
# Required markers (see docs/design/traceability-verbs.tsv):
#   @shows <id> ...   the rule/engine/ADR this demo demonstrates (one or more)
# Optional:
#   @explains <id>    if the tape itself documents a concept
#
# Render: vhs docs/demo/<feature>.tape   (deps: vhs, ttyd, ffmpeg,
#         zsh-syntax-highlighting — see README.md)

# @shows REPLACE-WITH-RULE-ID
# @explains ADR-047

Output REPLACE-WITH-FEATURE.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1000
Set Height 640
Set FontSize 14
Set Padding 20
Set TypingSpeed 45ms

# --- Arrange (hidden): a clean, neutral, reproducible shell -----------------
# zsh + syntax-highlighting so typed commands are coloured and # comments go
# grey, the natural way (no personal dotfiles, no PII). A disposable repo in
# /tmp so no username leaks into any path. git colour forced on for the demo.
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

# --- Arrange (visible): state the starting point ----------------------------
Type "# Arrange: a fresh repo with the global keur pre-commit hook"
Enter
Sleep 1s

# --- Act: do the thing that should trigger the gate -------------------------
# Type "# Act: ..." then the command(s).

# --- Assert: show the expected block ----------------------------------------
# Type "# Assert: blocked, ..." and let the keur output stand.
