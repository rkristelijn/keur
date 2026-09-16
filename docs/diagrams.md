# Diagrams

Class and sequence diagrams for keur, grounded in the headers and
implementations under `src/keur/` and `src/rules/`.

## Class diagram: core types

Fields and their types are taken from `src/keur/adapters/{git,fs,proc}.h`,
`src/rules/rule_engine.h`, `src/keur/cmd/check.h`, and
`src/keur/report/report.h`. Adapter members are `std::function` seams defaulted
to the real implementation.

```mermaid
classDiagram
    class Git {
        +function~string(string)~ run
        +function~vector~string~()~ staged_files
        +default_run(string) string$
        +default_staged_files() vector~string~$
    }

    class Fs {
        +function~bool(string)~ exists
        +function~bool(string)~ mkdir_p
        +function~bool(string, string)~ write_executable
        +function~void(string)~ remove
    }

    class Proc {
        +function~int(string)~ run
        +function~ProcResult(string)~ capture
        +function~bool(string)~ has
    }

    class ProcResult {
        +int exit_code
        +string stdout_text
    }

    class Rule {
        +string id
        +string title
        +string category
        +string severity
        +string engine
        +string action
        +string confidence
        +string fix
        +RuleTarget target
        +vector~RulePattern~ patterns
    }

    class RuleTarget {
        +vector~string~ extensions
        +vector~string~ filenames
        +vector~string~ exclude_paths
        +string content_contains
    }

    class RulePattern {
        +string regex_str
        +string message
    }

    class RuleFinding {
        +string rule_id
        +string title
        +string category
        +string severity
        +string action
        +string confidence
        +string file
        +int line
        +string message
    }

    class CheckOptions {
        +Tier tier
        +string ai
        +string config
    }

    class PolicyInput {
        +bool has_secret_block
        +bool has_rule_block
        +bool has_any_finding
    }

    class ReportOptions {
        +OutputFormat format
        +ColorMode color
    }

    Proc ..> ProcResult : capture returns
    Rule *-- RuleTarget
    Rule *-- RulePattern
    Rule ..> RuleFinding : engine produces
```

Notes:

- `PolicyInput` is consumed by the pure `policy_evaluate(const PolicyInput&) ->
  Verdict`. `has_secret_block` and `has_rule_block` both force BLOCK.
- `RuleFinding` is serialised by the report provider (`finding_to_json`), which
  is the single definition of keur's finding JSON.
- `CheckOptions` uses `Tier` (`PreCommit` / `PrePush`). `ReportOptions` uses
  `OutputFormat` (`Jsonl` / `Compact` / `Verbose`) and `ColorMode` (`Auto` /
  `Always` / `Never`).

## Sequence: keur check on commit

Grounded in `src/keur/main.cpp`, `src/keur/cmd/check.cpp`, and
`src/keur/adapters/git.cpp`.

```mermaid
sequenceDiagram
    actor Dev as Developer
    participant Git as git
    participant Hook as pre-commit stub
    participant Main as keur main
    participant Check as cmd::check
    participant GitAd as Git adapter
    participant Engine as rule engine
    participant Policy as policy_evaluate

    Dev->>Git: git commit
    Git->>Hook: run .git/hooks/pre-commit
    Hook->>Main: exec keur check --tier pre-commit
    Main->>Check: check(opts)
    Check->>GitAd: staged_files()
    GitAd->>Git: git diff --cached --name-only --diff-filter=ACM -z
    Git-->>GitAd: staged file list
    GitAd-->>Check: vector<string>
    alt no staged files
        Check-->>Hook: print PASS, return 0
    else files present
        Check->>Engine: rules_load(dirs) + rules_scan_files(rules, files)
        Engine-->>Check: vector<RuleFinding>
        Check->>Check: build PolicyInput from findings
        Check->>Policy: policy_evaluate(input)
        Policy-->>Check: Verdict
        alt Block
            Check-->>Hook: print BLOCK, return 1
            Hook-->>Git: exit 1
            Git-->>Dev: commit aborted
        else Warn or Pass
            Check-->>Hook: print WARN/PASS, return 0
            Hook-->>Git: exit 0
            Git-->>Dev: commit proceeds
        end
    end
```

## Sequence: keur hook --install

Grounded in `src/keur/cmd/hook.cpp` and `src/keur/adapters/{git,fs}.cpp`.

```mermaid
sequenceDiagram
    actor Dev as Developer
    participant Main as keur main
    participant Hook as cmd::hook
    participant GitAd as Git adapter
    participant FsAd as Fs adapter

    Dev->>Main: keur hook --install
    Main->>Hook: hook(opts, deps)
    Hook->>GitAd: git_repo_hooks_dir()
    GitAd->>GitAd: run("rev-parse --git-dir") + "/hooks"
    GitAd-->>Hook: hooks dir (or "")
    alt not a git repo
        Hook-->>Dev: error "not inside a git repository", return 1
    else in a repo
        Hook->>FsAd: mkdir_p(hooks dir)
        FsAd-->>Hook: ok
        loop pre-commit, pre-push
            Hook->>FsAd: write_executable(dir/name, stub)
            FsAd-->>Hook: ok
        end
        Hook-->>Dev: "Per-repo KEUR hooks installed", return 0
    end
```

The stub written by `write_executable` is minimal and contains no logic:

```sh
#!/bin/sh
exec keur check --tier pre-commit
```

`--global` follows the same shape but targets `global_hooks_dir()`
(`$XDG_CONFIG_HOME/git/hooks`) and then sets `core.hooksPath` through the Proc
adapter, unless it is already set to a different directory.
