# ai-slop-fake-test.tape — keur catches a test that asserts nothing (ADR-047).
# @shows SLOP-107
# @explains ADR-036
#
# The modern "my linter can't do that" moment. AI-generated tests often LOOK
# like tests but verify nothing: `expect(x).toBeTruthy()` passes for any
# non-falsy value. jest runs it GREEN; ESLint sees valid syntax. keur flags it
# as a weak assertion — the one tool that tells a real test from a fake one.
#
# Hermetic via sandbox.sh. (SLOP-107 is advisory — detection, not a block;
# the point is that it's SEEN at all, which no linter/runner does.)

Output ai-slop-fake-test.gif

Set Shell "zsh"
Set Theme "Catppuccin Mocha"
Set Width 1020
Set Height 700
Set FontSize 14
Set Padding 20
Set TypingSpeed 42ms

# --- Arrange (hidden): clean repo in /tmp, hermetic sandbox ------------------
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

Type "# Arrange: a repo with an AI-written test. Looks fine. jest would pass it."
Enter
Sleep 1400ms

# --- Act 1: a test that asserts nothing, scan it ----------------------------
Type "# Act 1: the 'test' — it runs green but verifies nothing"
Enter
Sleep 1s
Type `printf "test('parses the config', () => {\n  const result = parseConfig(raw);\n  expect(result).toBeTruthy();\n});\n" > config.test.js`
Enter
Sleep 600ms
Type "$SB keur scan config.test.js | grep SLOP-107"
Enter
Sleep 3000ms

# --- Act 2: ask keur what's wrong with it -----------------------------------
Type "# Act 2: ESLint & jest are happy. ask keur why it isn't"
Enter
Sleep 1200ms
Type "$SB keur learn SLOP-107"
Enter
Sleep 3500ms

# --- Act 3: replace with an assertion that verifies the actual value --------
Type "# Act 3: assert the actual value, as keur advised"
Enter
Sleep 1200ms
Type `printf "test('parses the config', () => {\n  const result = parseConfig(raw);\n  expect(result.port).toBe(8080);\n});\n" > config.test.js`
Enter
Sleep 600ms
Type "$SB keur scan config.test.js | grep -c SLOP-107"
Enter
Sleep 2500ms

# --- Assert: 0 — the test now checks something real -------------------------
Type "# Assert: 0 — a real assertion. The green is now worth something."
Enter
Sleep 2800ms
