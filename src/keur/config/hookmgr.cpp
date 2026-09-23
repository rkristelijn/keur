/**
 * @file config/hookmgr.cpp
 * @brief Pure detection of coexisting hook managers + effective hook owner.
 *        No I/O of its own: git + fs are injected via HookProbe, so the whole
 *        decision is unit-testable against a spoofed repo. @implements KEUR-HOOKMGR
 */
#include "config/hookmgr.h"

#include "common/platform.h"

namespace keur::config {

namespace {

// Known managers and the repo-relative signature that identifies each. Adding
// a manager is one row here (data, not branching).
struct KnownManager {
    const char* name;
    const char* signature;   // repo-relative path to test for existence
    HookOwner   owner;
    HookKind    kind;
};

const KnownManager k_known[] = {
    // Managers — own/install git hooks.
    {"husky",           ".husky",                  HookOwner::Husky,      HookKind::Manager},
    {"pre-commit",      ".pre-commit-config.yaml", HookOwner::PreCommit,  HookKind::Manager},
    {"lefthook",        "lefthook.yml",            HookOwner::Lefthook,   HookKind::Manager},
    {"overcommit",      ".overcommit.yml",         HookOwner::Overcommit, HookKind::Manager},
    {"simple-git-hooks", ".simple-git-hooks.json", HookOwner::Other,      HookKind::Manager},
    {"githooks",        ".githooks",               HookOwner::Other,      HookKind::Manager},
    // Runners — run INSIDE a hook, don't own it (context, not an owner).
    {"lint-staged",     ".lintstagedrc",           HookOwner::Other,      HookKind::Runner},
    {"lint-staged-json", ".lintstagedrc.json",     HookOwner::Other,      HookKind::Runner},
    {"commitlint",      "commitlint.config.js",    HookOwner::Other,      HookKind::Runner},
    {"commitlint-rc",   ".commitlintrc.json",      HookOwner::Other,      HookKind::Runner},
    {"commitizen",      ".czrc",                   HookOwner::Other,      HookKind::Runner},
};

}  // namespace

std::string owner_name(HookOwner owner) {
    switch (owner) {
        case HookOwner::None:       return "none";
        case HookOwner::Keur:       return "keur";
        case HookOwner::Husky:      return "husky";
        case HookOwner::PreCommit:  return "pre-commit";
        case HookOwner::Lefthook:   return "lefthook";
        case HookOwner::Overcommit: return "overcommit";
        case HookOwner::Other:      return "other";
    }
    return "unknown";
}

HookSituation detect_hook_situation(const HookProbe& probe) {
    HookSituation s;

    // Resolve core.hooksPath: repo-local wins over global (git's own precedence).
    s.local_hooks_path  = probe.git_config("config --local core.hooksPath");
    s.global_hooks_path = probe.git_config("config --global core.hooksPath");
    s.effective_path = !s.local_hooks_path.empty() ? s.local_hooks_path
                                                    : s.global_hooks_path;

    // Probe each known tool by its signature file under the repo root.
    for (const auto& km : k_known) {
        std::string sig = probe.repo_root.empty()
                              ? std::string(km.signature)
                              : platform::path_join(probe.repo_root, km.signature);
        bool present = probe.exists(sig);
        s.managers.push_back({km.name, km.signature, km.kind, present});
    }

    // Decide the effective owner. A repo-local core.hooksPath pointing into a
    // manager's dir means that manager owns hooks now (this is the Husky case:
    // core.hooksPath = .husky/_). Otherwise, a detected manager owns them; else
    // an effective path (keur's global) makes keur the owner; else nobody.
    auto path_points_into = [&](const char* frag) {
        return !s.effective_path.empty() &&
               s.effective_path.find(frag) != std::string::npos;
    };

    if (path_points_into(".husky"))          s.owner = HookOwner::Husky;
    else if (path_points_into(".pre-commit")) s.owner = HookOwner::PreCommit;
    else {
        // No manager-owned path: fall back to signature detection, then keur.
        // Runners (lint-staged, commitlint, …) never own hooks — skip them.
        HookOwner detected_owner = HookOwner::None;
        for (size_t i = 0; i < s.managers.size(); ++i)
            if (s.managers[i].detected && k_known[i].kind == HookKind::Manager) {
                detected_owner = k_known[i].owner; break;
            }

        if (detected_owner != HookOwner::None && !s.local_hooks_path.empty())
            s.owner = detected_owner;                 // local path + a manager present
        else if (!s.effective_path.empty())
            s.owner = HookOwner::Keur;                // keur's (global) hook is what runs
        else if (detected_owner != HookOwner::None)
            s.owner = detected_owner;                 // manager present, default .git/hooks
        else
            s.owner = HookOwner::None;
    }

    return s;
}

}  // namespace keur::config
