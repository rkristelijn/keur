/**
 * @file git.cpp
 * @brief Git adapter implementation — the only place keur spawns git.
 */
#include "git.h"

#include <cstdio>
#include <string>
#include <vector>

namespace keur {

std::string Git::default_run(const std::string& args) {
    std::string cmd = "git " + args + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string out;
    char buf[256];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

std::vector<std::string> Git::default_staged_files() {
    std::vector<std::string> files;
    // -z: NUL-separated to survive filenames with spaces.
    FILE* p = popen(
        "git diff --cached --name-only --diff-filter=ACM -z 2>/dev/null", "r");
    if (!p) return files;
    std::string cur;
    int c;
    while ((c = fgetc(p)) != EOF) {
        if (c == '\0') {
            if (!cur.empty()) { files.push_back(cur); cur.clear(); }
        } else {
            cur += static_cast<char>(c);
        }
    }
    if (!cur.empty()) files.push_back(cur);
    pclose(p);
    return files;
}

std::string git_repo_hooks_dir(const Git& git) {
    std::string git_dir = git.run("rev-parse --git-dir");
    if (git_dir.empty()) return "";
    return git_dir + "/hooks";
}

}  // namespace keur
