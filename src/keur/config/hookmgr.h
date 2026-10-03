/**
 * @file config/hookmgr.h
 * @brief Detect coexisting git hook managers (Husky, pre-commit, lefthook,
 *        Overcommit) and who effectively owns this repo's hooks.
 *
 * keur is not the only tool that claims .git/hooks / core.hooksPath. A repo may
 * run Husky (.husky/, sets a repo-local core.hooksPath), pre-commit
 * (.pre-commit-config.yaml), lefthook (lefthook.yml), or Overcommit
 * (.overcommit.yml). keur must DETECT and coexist, never silently clobber.
 *
 * The subtlety this module gets right (and the old hook --status missed): git
 * uses the REPO-LOCAL core.hooksPath over the global one. So a repo with Husky
 * (core.hooksPath = .husky/_) runs Husky even when keur set a *global*
 * core.hooksPath — keur's global hook is shadowed there. This module reports
 * the EFFECTIVE owner, not just "is a global hook installed".
 *
 * Pure + injectable: git queries via a `run` callable, file existence via an
 * `exists` callable, so the whole detection is unit-testable with a spoofed
 * repo (no real .husky/, no real git). @implements ADR-003
 * @id KEUR-HOOKMGR
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace keur::config {

/// What role a detected tool plays in the hook system:
///   Manager — owns/installs git hooks (husky, pre-commit, lefthook, …).
///   Runner  — runs INSIDE a hook, doesn't own it (lint-staged, commitlint, …).
enum class HookKind { Manager, Runner };

/// A known third-party hook tool and whether this repo uses it.
struct HookManager {
    std::string name;        ///< "husky" | "lint-staged" | "commitlint" | …
    std::string signature;   ///< the file/dir that identifies it (e.g. ".husky/")
    HookKind    kind = HookKind::Manager;  ///< manager (owns hooks) vs runner (in a hook)
    bool        detected = false;

    HookManager() = default;
    // Explicit constructor so vector::emplace_back works on every toolchain:
    // libc++ (macOS Xcode CI) cannot emplace a constructor-less aggregate.
    // @see KEUR-PORT-007
    HookManager(std::string name_, std::string signature_,
                HookKind kind_ = HookKind::Manager, bool detected_ = false)
        : name(std::move(name_)), signature(std::move(signature_)),
          kind(kind_), detected(detected_) {}
};

/// Where git will actually look for hooks in this repo, and who owns that path.
enum class HookOwner { None, Keur, Husky, PreCommit, Lefthook, Overcommit, Other };

/// The effective hook situation for a repo.
struct HookSituation {
    std::string local_hooks_path;   ///< repo-local core.hooksPath ("" if unset)
    std::string global_hooks_path;  ///< global core.hooksPath ("" if unset)
    std::string effective_path;     ///< what git actually uses (local wins)
    HookOwner   owner = HookOwner::None;
    std::vector<HookManager> managers;  ///< all known managers + detected flag
};

/// Callables the detector needs — spoofable in tests.
struct HookProbe {
    std::function<std::string(const std::string& git_args)> git_config;  ///< run `git <args>`, trimmed
    std::function<bool(const std::string& path)> exists;                 ///< path exists?
    std::string repo_root;                                               ///< git toplevel ("" = not a repo)
};

/// Human name of an owner, for display/JSON.
[[nodiscard]] std::string owner_name(HookOwner owner);

/// Detect the effective hook situation: resolve local-over-global core.hooksPath,
/// probe each known manager's signature file, and decide who owns the hooks.
[[nodiscard]] HookSituation detect_hook_situation(const HookProbe& probe);

}  // namespace keur::config
