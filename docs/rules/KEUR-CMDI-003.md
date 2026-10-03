---
reviewed: true
---

# KEUR-CMDI-003 — OS command injection via the os.popen family
category: cmdi · severity: critical · CWE-78

## Why this matters
`os.popen` and its siblings (`subprocess.getoutput`, `commands.getoutput`, …)
run their argument through a shell. When any part of that argument is a
variable, a concatenation, or an interpolation, an attacker who influences it
can inject shell metacharacters (`;`, `|`, `` ` ``, `$( )`) and run arbitrary
commands. A fixed string literal is safe; an assembled command string is not.

## Unsafe (what fires)
```py
def run_cmd(cmd):
    return os.popen(cmd).read()          # variable arg → shell injection

os.popen("ping " + host)                 # concatenation → injection
subprocess.getoutput(f"ls {path}")       # interpolation → injection
```

## Safe (what does not)
```py
subprocess.run(["ping", "-c", "1", host])   # argv vector, no shell
os.popen("ls -la")                          # a fixed literal does not fire
```

## Fix
Pass a shell-free argument vector to `subprocess.run([...])` (without
`shell=True`) so the OS receives argv directly and never parses a command
string. Never hand an assembled string to `os.popen` / `getoutput`. If you
truly need a shell feature, whitelist and escape the untrusted parts explicitly.

## Suppress (if genuinely a false positive)
`# keur:ignore KEUR-CMDI-003`  (or a `.keurignore` entry — ADR-035)

## References
- CWE-78: Improper Neutralization of Special Elements used in an OS Command
- OWASP: Command Injection
- Python docs: `subprocess` — "Security Considerations" (avoid `shell=True`)
