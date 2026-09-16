/**
 * @file cmd_hook.cpp
 * @brief keur hook — install / manage keur git hooks.
 */
#include "cmd_hook.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

#include "git.h"

namespace keur {

// ---------------------------------------------------------------------------
// Hook stub templates
// ---------------------------------------------------------------------------

static constexpr std::array<const char*, 2> k_hook_names = {"pre-commit", "pre-push"};

// The stub is intentionally minimal: no logic, just exec keur check.
static std::string hook_stub(const char* hook_name) {
    std::string tier = (std::strcmp(hook_name, "pre-push") == 0) ? "pre-push" : "pre-commit";
    return std::string("#!/bin/sh\nexec keur check --tier ") + tier + "\n";
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool mkdir_p(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    // Try creating parent first
    auto sep = path.rfind('/');
    if (sep != std::string::npos && sep > 0) {
        if (!mkdir_p(path.substr(0, sep))) return false;
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

static bool file_exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

static bool write_executable(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) return false;
    f << content;
    f.close();
    chmod(path.c_str(), 0755);
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string global_hooks_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base = xdg ? std::string(xdg)
                           : (std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.config");
    return base + "/git/hooks";
}

std::string repo_hooks_dir(const Git& git) {
    // Delegate to the git adapter (single git touch point). Uses --git-dir so
    // a global core.hooksPath does not redirect per-repo installs.
    return git_repo_hooks_dir(git);
}

bool hooks_install(const std::string& hooks_dir) {
    if (!mkdir_p(hooks_dir)) {
        std::cerr << "keur: could not create hooks directory: " << hooks_dir << "\n";
        return false;
    }
    bool ok = true;
    for (auto* name : k_hook_names) {
        std::string dest = hooks_dir + "/" + name;
        if (!write_executable(dest, hook_stub(name))) {
            std::cerr << "keur: failed to write hook: " << dest << "\n";
            ok = false;
        } else {
            std::cout << "  installed " << name << " -> " << dest << "\n";
        }
    }
    return ok;
}

void hooks_remove(const std::string& hooks_dir) {
    for (auto* name : k_hook_names) {
        std::string path = hooks_dir + "/" + name;
        if (file_exists(path)) {
            unlink(path.c_str());
            std::cout << "  removed " << path << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// cmd_hook_parse
// ---------------------------------------------------------------------------

HookOptions cmd_hook_parse(const std::vector<std::string>& args) {
    HookOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--install")        { o.action = HookAction::Install; }
        else if (args[i] == "--global")    {
            // --global alone means "install globally"; with --remove means remove global
            if (o.action == HookAction::Remove) o.global = true;
            else o.action = HookAction::Global;
        }
        else if (args[i] == "--status")    { o.action = HookAction::Status; }
        else if (args[i] == "--remove") {
            o.action = HookAction::Remove;
            // peek at next arg for optional --global
            if (i + 1 < args.size() && args[i + 1] == "--global") {
                o.global = true;
                ++i;
            }
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// cmd_hook
// ---------------------------------------------------------------------------

int cmd_hook(const HookOptions& opts, const Git& git) {
    switch (opts.action) {
        case HookAction::Install: {
            std::string dir = repo_hooks_dir(git);
            if (dir.empty()) {
                std::cerr << "keur hook: not inside a git repository\n";
                return 1;
            }
            std::cout << "Installing per-repo KEUR hooks...\n";
            if (!hooks_install(dir)) return 1;
            std::cout << "Per-repo KEUR hooks installed. They run 'keur check' on commit/push.\n";
            return 0;
        }

        case HookAction::Global: {
            std::string dir = global_hooks_dir();
            std::cout << "Installing global KEUR hooks...\n";
            if (!hooks_install(dir)) return 1;
            // Configure git core.hooksPath if not already set to this dir
            std::string current = git.run("config --global core.hooksPath");
            if (!current.empty() && current != dir) {
                std::cerr << "keur hook: core.hooksPath already set to: " << current << "\n";
                std::cerr << "  Leaving it unchanged. Merge KEUR hooks into that dir, or unset it.\n";
            } else {
                std::string cmd = "git config --global core.hooksPath " + dir;
                system(cmd.c_str());  // NOLINT(cert-env33-c)
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
                std::string p = gdir + "/" + name;
                std::cout << "  global " << name << ": " << (file_exists(p) ? "installed" : "(absent)") << "\n";
            }
            if (!rdir.empty()) {
                for (auto* name : k_hook_names) {
                    std::string p = rdir + "/" + name;
                    std::cout << "  repo   " << name << ": " << (file_exists(p) ? "installed" : "(absent)") << "\n";
                }
            }
            return 0;
        }

        case HookAction::Remove: {
            if (opts.global) {
                hooks_remove(global_hooks_dir());
                std::cerr << "keur: Left core.hooksPath unchanged. Unset with: "
                             "git config --global --unset core.hooksPath\n";
            } else {
                std::string dir = repo_hooks_dir(git);
                if (dir.empty()) {
                    std::cerr << "keur hook: not inside a git repository\n";
                    return 1;
                }
                hooks_remove(dir);
            }
            return 0;
        }
    }
    return 0;
}

}  // namespace keur
