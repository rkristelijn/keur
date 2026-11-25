# install-curl.tape — install.sh: download, checksum-verify BEFORE trust, run.
# @shows ADR-045 SCA-030
# @explains ADR-047
#
# Authored by action-ai (dist-channels domain). Works against the public v0.2.0
# release today (pre-release safe). Asserts on `keur version`, not install.sh's
# slot-echo. XDG_CONFIG_HOME is isolated to a temp dir so `keur version`
# shows the binary's real version (0.2.0), not the renderer's global config.

Output install-curl.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1000
Set Height 660
Set FontSize 14
Set Padding 20
Set TypingSpeed 45ms

# --- Arrange (hidden): clean shell, disposable dir, no keur yet -------------
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
Type "rm -rf /tmp/demo-install-curl && mkdir -p /tmp/demo-install-curl && cd /tmp/demo-install-curl"
Enter
Type "export KEUR_BIN_DIR=/tmp/demo-install-curl/bin KEUR_VERSION=v0.2.0"
Enter
Type "export XDG_CONFIG_HOME=/tmp/demo-install-curl/xdg"
Enter
Type "clear"
Enter
Show

Type "# Arrange: no keur yet — fetch the installer, read it, run it"
Enter
Sleep 1200ms

# --- Act: download install.sh (never pipe curl|sh unverified) ---------------
Type "# Act: download install.sh — never pipe curl|sh unverified"
Enter
Sleep 1s
Type "curl -fsSLO https://raw.githubusercontent.com/rkristelijn/keur/v0.2.0/install.sh"
Enter
Sleep 1500ms
Type "sh install.sh"
Enter
Sleep 3500ms

# --- Assert: checksum verified before install, then keur runs ---------------
Type "# Assert: checksum verified BEFORE install — then keur runs"
Enter
Sleep 1s
Type "/tmp/demo-install-curl/bin/keur version"
Enter
Sleep 2500ms
