# global-hooks-block.tape — vhs demo (render: vhs demos/global-hooks-block.tape)
# @shows SEC-100 KEUR-SECRET-003
# @explains ADR-047
# Shows the global keur pre-commit hook blocking a .env with a plaintext secret.
# @shows points at the rules this demo demonstrates (both are declared rule ids).

Output global-hooks-block.gif

Set Shell "bash"
Set Width 1000
Set Height 640
Set FontSize 14
Set Padding 20
Set TypingSpeed 40ms

# --- setup (hidden): clean disposable repo, no prompt noise ---
Hide
Type "export PS1='$ '"
Enter
Type "rm -rf ~/tmp/demo-repo && mkdir -p ~/tmp/demo-repo && cd ~/tmp/demo-repo"
Enter
Type "clear"
Enter
Show

Type "echo 'Demo: global git hooks block plaintext secrets'"
Enter
Sleep 1s

Type "git init"
Enter
Sleep 800ms

Type "echo 'password=1234' > .env"
Enter
Sleep 600ms

Type "git add ."
Enter
Sleep 600ms

Type 'git commit -m "chore: init"'
Enter
Sleep 2500ms

Type "echo 'exit code:' $?"
Enter
Sleep 2500ms
