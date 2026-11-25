# install-npx.tape — zero-install via npx: lazy fetch + checksum, then gate.
# @shows ADR-045
# @explains ADR-047
#
# Authored by action-ai (dist-channels domain), verified via local npm pack.
# NOTE: the npx path works PUBLICLY only after the v0.3.0 npm-publish.
# Pre-release: render via local `npm pack` in packaging/npm, or use install-curl.

Output install-npx.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1000
Set Height 640
Set FontSize 14
Set Padding 20
Set TypingSpeed 45ms

# --- Arrange (hidden): clean shell, disposable repo, no keur installed -------
Hide
Type "unset KEUR_RULES_DIRS"
Enter
Type "setopt interactive_comments"
Enter
Type "source /opt/homebrew/share/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh"
Enter
Type "export CLICOLOR_FORCE=1 CLICOLOR=1"
Enter
Type "PROMPT='%F{green}❯%f '"
Enter
Type "rm -rf /tmp/demo-install-npx && mkdir -p /tmp/demo-install-npx && cd /tmp/demo-install-npx"
Enter
Type "git init -q && git config color.ui always"
Enter
Type "clear"
Enter
Show

Type "# Arrange: no keur on the machine — just Node and a fresh repo"
Enter
Sleep 1200ms

# --- Act: run keur via npx, no prior install --------------------------------
Type "# Act: run keur with npx — no prior install"
Enter
Sleep 1s
Type "npx --yes keur check"
Enter
Wait@30s /❯\s*$/
Sleep 4s

# --- Assert: the checksum-verified binary ran, empty stage is a PASS --------
Type "# Assert: the checksum-verified binary ran — empty stage is a PASS"
Enter
Sleep 2500ms
