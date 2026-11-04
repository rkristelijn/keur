/**
 * @file adapters/git.cpp
 * @brief Git adapter implementation — the only place keur spawns git. The
 *        spawn/capture primitives come from platform::, so there is no raw
 *        popen or POSIX-only redirect here.
 */
#include "adapters/git.h"

#include <set>
#include <string>
#include <vector>

#include "common/platform.h"

namespace keur::adapters {

namespace {
// Split a git argument string into whitespace-separated tokens. git subcommands
// are always simple token lists ("rev-parse --git-dir"), so a plain split is
// correct — and it lets us run git shell-free via an argv vector, with no
// /bin/sh to interpret metacharacters (CWE-78). @see KEUR-SEC-006.
std::vector<std::string> split_args(const std::string& args) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : args) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
}  // namespace

std::string Git::default_run(const std::string& args) {
    // Shell-free: argv = {"git", <tokens...>} spawned without a shell.
    std::vector<std::string> argv{"git"};
    for (auto& t : split_args(args)) argv.push_back(std::move(t));
    auto cap = platform::capture_argv(argv);
    std::string out = std::move(cap.out);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

std::vector<std::string> Git::default_staged_files() {
    std::vector<std::string> files;
    // -z: NUL-separated to survive filenames with spaces. Shell-free argv.
    std::string raw;
    platform::capture_bytes_argv(
        {"git", "diff", "--cached", "--name-only", "--diff-filter=ACM", "-z"}, raw);
    std::string cur;
    for (char c : raw) {
        if (c == '\0') {
            if (!cur.empty()) { files.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) files.push_back(cur);
    return files;
}

bool Git::default_merge_in_progress() {
    // A merge is in progress iff .git/MERGE_HEAD resolves. `rev-parse -q
    // --verify MERGE_HEAD` prints the hash and exits 0 when it exists, prints
    // nothing on exit non-zero otherwise. We key off the (non-empty) stdout so
    // we reuse the same shell-free capture seam as the rest of the adapter —
    // no direct .git path poking, which keeps it correct under worktrees and a
    // custom $GIT_DIR. @implements ADR-068
    return !default_run("rev-parse -q --verify MERGE_HEAD").empty();
}

namespace {
// NUL-split a raw `-z` git output buffer into a file list (shared by the merge-
// scoped query below and staged_files()'s pattern). Drops empty segments.
std::vector<std::string> split_nul(const std::string& raw) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : raw) {
        if (c == '\0') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
}  // namespace

std::vector<std::string> Git::default_merge_scoped_files() {
    // "What did THIS developer's side contribute to the merge" — the honest
    // scope for the gate (ADR-068 Decision 2). It is the merge-base THREE-DOT
    // diff, NOT `--cached --diff-filter=M`.
    //
    // WHY three-dot-vs-M (the iteration-1 defect): during a merge the index
    // holds the full merged tree, so `--cached --diff-filter=M` reports every
    // path that ended up Modified relative to HEAD — which on a WIDE merge is
    // every file BOTH sides touched, most of them the incoming side's work that
    // was already gated on main. Measured on the container-images merge that
    // motivated ADR-068: `--cached --diff-filter=M` returned 50 files (still
    // full of other teams' pre-existing findings), whereas the three-dot diff
    // against the merge-base returned EXACTLY the 9 files the developer changed.
    // `--diff-filter=M` conflates "both sides touched it" with "I changed it".
    //
    // The correct set:
    //   base   = git merge-base HEAD MERGE_HEAD     (fork point of the 2 parents)
    //   scoped = git diff --name-only -z base...HEAD (three-dot: HEAD's side's
    //                                                 additions since the fork)
    // then INTERSECTED with the staged set so a file in the branch diff but not
    // actually staged on THIS commit is never scanned, and -z NUL-splitting is
    // kept to survive filenames with spaces.
    //
    // Fail-safe: if MERGE_HEAD or the merge-base cannot be resolved (not a real
    // merge, shallow clone with no common ancestor, etc.) fall back to the full
    // staged set rather than crash or silently scan nothing — never under-gate
    // by accident. @implements ADR-068 (Decision 2)
    std::string merge_head = default_run("rev-parse -q --verify MERGE_HEAD");
    if (merge_head.empty()) return default_staged_files();  // not a merge → safe

    std::string base = default_run("merge-base HEAD " + merge_head);
    if (base.empty()) return default_staged_files();  // no common ancestor → safe

    std::string raw;
    platform::capture_bytes_argv(
        {"git", "diff", "--name-only", "-z", base + "...HEAD"}, raw);
    std::vector<std::string> branch_files = split_nul(raw);
    // An EMPTY three-dot diff is a valid, resolved answer — not an unresolvable
    // state. It means THIS developer's side (base...HEAD) added nothing of its
    // own to the merge; everything staged is the incoming side's work, which was
    // already gated on main. Returning the full staged set here would re-gate
    // exactly the work ADR-068 Decision 2 exists to skip. The only genuinely
    // unresolvable states (no MERGE_HEAD, no merge-base) already fell back
    // above; a successfully computed empty diff returns an empty scope cleanly.
    // @implements ADR-068 (Decision 2)
    if (branch_files.empty()) return {};

    // Intersect with the staged set: only files actually part of THIS commit.
    std::vector<std::string> staged = default_staged_files();
    std::set<std::string> staged_set(staged.begin(), staged.end());
    std::vector<std::string> files;
    for (const auto& f : branch_files)
        if (staged_set.count(f)) files.push_back(f);
    return files;
}

std::string Git::default_current_branch() {
    // "HEAD" means detached (no branch) — treat as no current branch.
    std::string b = default_run("rev-parse --abbrev-ref HEAD");
    return b == "HEAD" ? "" : b;
}

std::string Git::default_default_branch() {
    // Prefer the remote's declared default (origin/HEAD -> origin/<branch>).
    // e.g. "refs/remotes/origin/main" or "origin/main"; take the last segment.
    std::string ref = default_run("symbolic-ref --quiet refs/remotes/origin/HEAD");
    if (!ref.empty()) {
        auto slash = ref.find_last_of('/');
        return slash == std::string::npos ? ref : ref.substr(slash + 1);
    }
    // No remote HEAD (fresh/local repo): fall back to the configured init
    // default, then the conventional "main". This is a best-effort name, used
    // only to WARN/BLOCK on a direct commit — never to mutate anything.
    std::string cfg = default_run("config --get init.defaultBranch");
    return cfg.empty() ? "main" : cfg;
}

std::string git_repo_hooks_dir(const Git& git) {
    std::string git_dir = git.run("rev-parse --git-dir");
    if (git_dir.empty()) return "";
    return platform::path_join(git_dir, "hooks");
}}  // namespace keur::adapters
