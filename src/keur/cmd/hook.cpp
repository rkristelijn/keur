/**
 * @file cmd/hook.cpp
 * @brief keur hook — install / manage keur git hooks.
 *
 * All I/O goes through injectable adapters (git / fs / proc), so hook is
 * fully spoofable: tests exercise every branch (including --global) without a
 * real repo, real files, or real process spawns.
 */
#include "cmd/hook.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "common/platform.h"
#include "config/hookmgr.h"

namespace keur::cmd {

// ---------------------------------------------------------------------------
// Hook stub templates
// ---------------------------------------------------------------------------

static constexpr std::array<const char*, 2> k_hook_names = {"pre-commit", "pre-push"};

// Per-repo hook stub: minimal, just run keur for this repo's commits.
static std::string hook_stub(const char* hook_name) {
    std::string tier = (std::strcmp(hook_name, "pre-push") == 0) ? "pre-push" : "pre-commit";
    return std::string("#!/bin/sh\nexec keur check --tier ") + tier + "\n";
}

// Global hook stub: an orchestrator that gives each repo control over how keur
// composes with the repo's own hooks. Because core.hooksPath disables the
// repo's .git/hooks, this stub resolves and chains to the repo's .githooks/
// itself. The repo declares the relationship in keur.toml:
//
//   [hooks] mode = "augment"   (default) run keur, then the repo hook
//                = "replace"   run only the repo hook (repo overrides global)
//                = "keur-only" run only keur (ignore the repo hook)
//
// The repo hook is expected at .githooks/<hook> (a common convention that is
// not shadowed by core.hooksPath).
static std::string global_hook_stub(const char* hook_name) {
    std::string tier = (std::strcmp(hook_name, "pre-push") == 0) ? "pre-push" : "pre-commit";
    std::string h(hook_name);
    return
        "#!/bin/sh\n"
        "# keur global hook orchestrator. Composes keur with the repo's own\n"
        "# hooks; the repo decides via [hooks].mode in keur.toml.\n"
        "root=$(git rev-parse --show-toplevel 2>/dev/null)\n"
        "mode=augment\n"
        "if [ -n \"$root\" ] && [ -f \"$root/keur.toml\" ]; then\n"
        "  m=$(sed -n 's/^[[:space:]]*mode[[:space:]]*=[[:space:]]*\"\\{0,1\\}\\([a-z-]*\\).*/\\1/p' \"$root/keur.toml\" | head -1)\n"
        "  [ -n \"$m\" ] && mode=$m\n"
        "fi\n"
        "repo_hook=\"$root/.githooks/" + h + "\"\n"
        "run_keur() { keur check --tier " + tier + "; }\n"
        "run_repo() { [ -x \"$repo_hook\" ] && \"$repo_hook\" \"$@\"; }\n"
        "case \"$mode\" in\n"
        "  replace)   run_repo \"$@\" ;;\n"
        "  keur-only) run_keur ;;\n"
        "  *)         run_keur && run_repo \"$@\" ;;\n"   // augment (default)
        "esac\n";
}

// ---------------------------------------------------------------------------
// Directory resolution
// ---------------------------------------------------------------------------

std::string global_hooks_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base = xdg ? std::string(xdg)
                           : platform::path_join(
                                 std::getenv("HOME") ? std::getenv("HOME") : "", ".config");
    return platform::path_join(platform::path_join(base, "git"), "hooks");
}

std::string repo_hooks_dir(const adapters::Git& git) {
    // --git-dir so a global core.hooksPath does not redirect per-repo installs.
    return adapters::git_repo_hooks_dir(git);
}

// ---------------------------------------------------------------------------
// Install / remove (via the adapters::Fs adapter)
// ---------------------------------------------------------------------------

bool hooks_install(const std::string& hooks_dir, const adapters::Fs& fs, bool global) {
    if (!fs.mkdir_p(hooks_dir)) {
        std::cerr << "keur: could not create hooks directory: " << hooks_dir << "\n";
        return false;
    }
    bool ok = true;
    for (auto* name : k_hook_names) {
        std::string dest = platform::path_join(hooks_dir, name);
        // Global installs get the orchestrator stub (composes keur with the
        // repo's own .githooks per its keur.toml mode); per-repo installs get
        // the minimal stub.
        std::string content = global ? global_hook_stub(name) : hook_stub(name);
        if (!fs.write_executable(dest, content)) {
            std::cerr << "keur: failed to write hook: " << dest << "\n";
            ok = false;
        } else {
            std::cout << "  installed " << name << " -> " << dest << "\n";
        }
    }
    return ok;
}

void hooks_remove(const std::string& hooks_dir, const adapters::Fs& fs) {
    for (auto* name : k_hook_names) {
        std::string path = platform::path_join(hooks_dir, name);
        if (fs.exists(path)) {
            fs.remove(path);
            std::cout << "  removed " << path << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// hook_parse
// ---------------------------------------------------------------------------

HookOptions hook_parse(const std::vector<std::string>& args) {
    HookOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--install")        { o.action = HookAction::Install; }
        else if (args[i] == "--global")    {
            if (o.action == HookAction::Remove) o.global = true;
            else o.action = HookAction::Global;
        }
        else if (args[i] == "--status")    { o.action = HookAction::Status; }
        else if (args[i] == "--remove") {
            o.action = HookAction::Remove;
            if (i + 1 < args.size() && args[i + 1] == "--global") {
                o.global = true;
                ++i;
            }
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// hook — orchestrator over the injected adapters
// ---------------------------------------------------------------------------

int hook(const HookOptions& opts, const HookDeps& deps) {
    const adapters::Git&  git  = deps.git;
    const adapters::Fs&   fs   = deps.fs;
    const adapters::Proc& proc = deps.proc;

    switch (opts.action) {
        case HookAction::Install: {
            std::string dir = repo_hooks_dir(git);
            if (dir.empty()) {
                std::cerr << "keur hook: not inside a git repository\n";
                return 1;
            }
            std::cout << "Installing per-repo KEUR hooks...\n";
            if (!hooks_install(dir, fs)) return 1;
            std::cout << "Per-repo KEUR hooks installed. They run 'keur check' on commit/push.\n";
            return 0;
        }

        case HookAction::Global: {
            std::string dir = global_hooks_dir();
            std::cout << "Installing global KEUR hooks...\n";
            if (!hooks_install(dir, fs, /*global=*/true)) return 1;
            std::string current = git.run("config --global core.hooksPath");
            if (!current.empty() && current != dir) {
                std::cerr << "keur hook: core.hooksPath already set to: " << current << "\n";
                std::cerr << "  Leaving it unchanged. Merge KEUR hooks into that dir, or unset it.\n";
            } else {
                // Shell-free: git + literal args + the path in argv, no
                // /bin/sh, so `dir` can never inject (CWE-78). @see KEUR-SEC-006.
                proc.run_argv(
                    {"git", "config", "--global", "core.hooksPath", dir}, {});
                std::cout << "  git core.hooksPath -> " << dir << "\n";
            }
            std::cout << "Global KEUR hooks installed for all repos on this machine.\n";
            return 0;
        }

        case HookAction::Status: {
            std::string gdir = global_hooks_dir();
            std::string rdir = repo_hooks_dir(git);
            std::cout << "Global core.hooksPath : " << git.run("config --global core.hooksPath") << "\n";
            std::cout << "Global hooks dir      : " << gdir << "\n";
            for (auto* name : k_hook_names) {
                std::string p = platform::path_join(gdir, name);
                std::cout << "  global " << name << ": " << (fs.exists(p) ? "installed" : "(absent)") << "\n";
            }
            if (!rdir.empty()) {
                for (auto* name : k_hook_names) {
                    std::string p = platform::path_join(rdir, name);
                    std::cout << "  repo   " << name << ": " << (fs.exists(p) ? "installed" : "(absent)") << "\n";
                }
            }

            // Coexisting hook managers (Husky, pre-commit, …). Resolves the
            // repo-LOCAL core.hooksPath (which git prefers over the global one),
            // so we report who EFFECTIVELY owns hooks here — not a misleading
            // "global installed" when e.g. Husky's local hooksPath shadows it.
            config::HookProbe probe;
            probe.git_config = [&](const std::string& a) { return git.run(a); };
            probe.exists = [&](const std::string& p) { return fs.exists(p); };
            probe.repo_root = git.run("rev-parse --show-toplevel");
            config::HookSituation hs = config::detect_hook_situation(probe);
            std::cout << "Effective hooks owner : " << config::owner_name(hs.owner);
            if (!hs.effective_path.empty()) std::cout << "  (core.hooksPath -> " << hs.effective_path << ")";
            std::cout << "\n";
            if (!hs.local_hooks_path.empty())
                std::cout << "  note: repo-local core.hooksPath overrides the global one\n";
            for (const auto& m : hs.managers)
                if (m.detected)
                    std::cout << "  detected manager: " << m.name << " (" << m.signature << ")\n";
            return 0;
        }

        case HookAction::Remove: {
            if (opts.global) {
                hooks_remove(global_hooks_dir(), fs);
                std::cerr << "keur: Left core.hooksPath unchanged. Unset with: "
                             "git config --global --unset core.hooksPath\n";
            } else {
                std::string dir = repo_hooks_dir(git);
                if (dir.empty()) {
                    std::cerr << "keur hook: not inside a git repository\n";
                    return 1;
                }
                hooks_remove(dir, fs);
            }
            return 0;
        }
    }
    return 0;
}

}  // namespace keur::cmd
