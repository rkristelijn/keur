/**
 * @file adapters/git.cpp
 * @brief Git adapter implementation — the only place keur spawns git. The
 *        spawn/capture primitives come from platform::, so there is no raw
 *        popen or POSIX-only redirect here.
 */
#include "adapters/git.h"

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

std::string git_repo_hooks_dir(const Git& git) {
    std::string git_dir = git.run("rev-parse --git-dir");
    if (git_dir.empty()) return "";
    return platform::path_join(git_dir, "hooks");
}

}  // namespace keur::adapters
