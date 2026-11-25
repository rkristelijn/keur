# ai-slop-fake-test.tape — keur catches a test that asserts nothing (ADR-047).
# @shows SLOP-107
# @explains ADR-036
#
# The modern "my linter can't do that" moment. AI-generated tests often LOOK
# like tests but verify nothing: expect(x).toBeTruthy() passes for any non-falsy
# value. jest runs it GREEN; ESLint sees valid syntax. keur flags it.
#
# Hermetic + PII-free: Arrange sets HOME/XDG to a neutral /tmp path (no username,
# no machine config) and runs keur from PATH (rules resolved relative to the
# binary, ADR-063). The test files are pre-staged FIXTURES copied in during the
# hidden Arrange and shown with `cat` — NOT typed via a live heredoc. A heredoc
# whose body has ()'{}| breaks under zsh-syntax-highlighting (the file silently
# isn't created → "No such file"); fixtures + cat sidestep that entirely.
# See docs/demo-authoring.md.

Output ai-slop-fake-test.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1020
Set Height 700
Set FontSize 14
Set Padding 20
Set TypingSpeed 42ms

# --- Arrange (hidden): neutral isolated env + disposable repo ---------------
# Stage the fixtures BEFORE sourcing highlighting / entering the demo dir, so
# the files exist without typing a heredoc on screen.
Hide
Type "unset KEUR_RULES_DIRS"
Enter
Type `F=/tmp/demo-ai-slop-fake-test-fixtures; rm -rf $F && mkdir -p $F && cp "$(git rev-parse --show-toplevel)"/docs/demo/fixtures/ai-slop-{fake,real}.test.js $F/`
Enter
Type "export HOME=/tmp/demo-ai-slop-fake-test-home XDG_CONFIG_HOME=/tmp/demo-ai-slop-fake-test-home/.config XDG_CACHE_HOME=/tmp/demo-ai-slop-fake-test-home/.cache"
Enter
Type "rm -rf /tmp/demo-ai-slop-fake-test-home /tmp/demo-ai-slop-fake-test && mkdir -p /tmp/demo-ai-slop-fake-test-home/.config /tmp/demo-ai-slop-fake-test && cd /tmp/demo-ai-slop-fake-test"
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

Type "# Arrange: a repo with an AI-written test. Looks fine. jest would pass it."
Enter
Sleep 1400ms

# --- Act 1: a test that asserts nothing, scan it ----------------------------
Type "# Act 1: the 'test' — it runs green but verifies nothing"
Enter
Sleep 1s
Type "cp /tmp/demo-ai-slop-fake-test-fixtures/ai-slop-fake.test.js config.test.js"
Enter
Type "cat config.test.js"
Enter
Sleep 1500ms
Type "keur scan config.test.js | grep SLOP-107"
Enter
Sleep 3000ms

# --- Act 2: ask keur what's wrong with it -----------------------------------
Type "# Act 2: ESLint & jest are happy. ask keur why it isn't"
Enter
Sleep 1200ms
Type "keur learn SLOP-107"
Enter
Sleep 3500ms

# --- Act 3: assert the actual value instead ---------------------------------
Type "# Act 3: assert the actual value, as keur advised"
Enter
Sleep 1200ms
Type "cp /tmp/demo-ai-slop-fake-test-fixtures/ai-slop-real.test.js config.test.js"
Enter
Type "cat config.test.js"
Enter
Sleep 1500ms
Type "keur scan config.test.js | grep -c SLOP-107"
Enter
Sleep 2500ms

# --- Assert: 0 — the test now checks something real -------------------------
Type "# Assert: 0 — a real assertion. The green is now worth something."
Enter
Sleep 2800ms
