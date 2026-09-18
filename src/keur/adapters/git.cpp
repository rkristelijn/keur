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

std::string Git::default_run(const std::string& args) {
    auto cap = platform::capture("git " + args);
    std::string out = std::move(cap.out);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

std::vector<std::string> Git::default_staged_files() {
    std::vector<std::string> files;
    // -z: NUL-separated to survive filenames with spaces.
    std::string raw;
    platform::capture_bytes(
        "git diff --cached --name-only --diff-filter=ACM -z", raw);
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
