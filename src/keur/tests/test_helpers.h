// keur:ignore-file SEC-041 — test support: contains patterns it checks for
/**
 * @file tests/test_helpers.h
 * @brief Shared helpers for keur's test translation units.
 *
 * The test suite is split across several .cpp files (one per feature cluster)
 * that all link into one doctest binary. The pure, universally-useful helpers
 * (tempdirs, fixture staging, repo paths, a findings predicate, the fake fs)
 * live here as `inline` so every TU can include them without ODR clashes. There
 * is NO mutable global state — each scenario makes and cleans its own tempdir —
 * so test files are order-independent and safe to split.
 *
 * Domain-specific helpers (manager model builders, hook-stub runners, the
 * scanner TOML fixture) stay in the TU that owns that feature, not here.
 *
 * @see docs/adr/ADR-002-single-cpp-binary.md
 */
#ifndef KEUR_TESTS_TEST_HELPERS_H
#define KEUR_TESTS_TEST_HELPERS_H

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rules/rule_engine.h"
#include "adapters/fs.h"

namespace keur::test {

inline std::string create_temp_dir() {
    char tmpl[] = "/tmp/keur_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    return d ? std::string(d) : "";
}

inline void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

inline std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline void cleanup_dir(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string full = dir + "/" + n;
        struct stat st{};
        stat(full.c_str(), &st);
        if (S_ISDIR(st.st_mode)) cleanup_dir(full);
        else unlink(full.c_str());
    }
    closedir(d);
    rmdir(dir.c_str());
}

inline bool file_exists_at(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

// Resolve the repo root. Prefer the compile-time KEUR_TEST_ROOT define
// (set by the Makefile); fall back to walking up from __FILE__ when that
// define is absent (e.g. ad-hoc compilation).
inline std::string repo_root() {
#ifdef KEUR_TEST_ROOT
    return KEUR_TEST_ROOT;
#else
    std::string f = __FILE__;
    auto p = f.rfind("/src/keur/");
    return (p != std::string::npos) ? f.substr(0, p) : ".";
#endif
}

inline std::string fixtures_dir() { return repo_root() + "/tests/fixtures"; }
inline std::string keur_rules_dir() { return repo_root() + "/rules"; }

// Does a findings vector contain a given rule id?
inline bool has_rule(const std::vector<keur::RuleFinding>& fs, const std::string& id) {
    for (auto& f : fs) if (f.rule_id == id) return true;
    return false;
}

// Stage a fixture file's content into a neutral tempdir path (no "tests/" or
// "fixtures/" in the path), returning the new absolute path. This mirrors a
// real repository layout so rules with exclude_paths (test/ tests/ fixtures/)
// are not filtered out. Caller owns the returned dir's parent via `out_dir`.
inline std::string stage_fixture(const std::string& fixture_rel,
                                 const std::string& dest_name,
                                 std::string& out_dir) {
    out_dir = create_temp_dir();
    std::string content = read_file(fixtures_dir() + "/" + fixture_rel);
    std::string dest = out_dir + "/" + dest_name;
    write_file(dest, content);
    return dest;
}

// An in-memory fake filesystem for fully-spoofed hook tests: no real files.
// Records writes/removes in a set so assertions can inspect what "happened".
struct FakeFsState {
    std::vector<std::string> written;
    std::vector<std::string> removed;
    std::vector<std::string> made_dirs;
};

inline keur::adapters::Fs make_fake_fs(FakeFsState& st) {
    keur::adapters::Fs fs;
    fs.mkdir_p = [&st](const std::string& p) { st.made_dirs.push_back(p); return true; };
    fs.write_executable = [&st](const std::string& p, const std::string&) {
        st.written.push_back(p); return true;
    };
    fs.remove = [&st](const std::string& p) { st.removed.push_back(p); };
    fs.exists = [&st](const std::string& p) {
        for (auto& w : st.written) if (w == p) return true;
        return false;
    };
    return fs;
}

// RAII tempdir — the 4th A of 4A testing (Arrange, Act, Assert, Annihilate).
// Annihilate is the symmetric counterpart of Arrange: whatever Arrange built,
// Annihilate tears down — and it must run whether or not the asserts pass. A
// manual cleanup_dir() at the end of a scenario is NOT symmetric: it is skipped
// whenever an earlier REQUIRE fails (doctest aborts via exception), so the
// tempdir leaks (we found 263 stale /tmp/keur_test_* dirs from exactly this).
// TempDir binds Arrange (ctor) and Annihilate (dtor) into one object, so the
// two phases can never drift apart and Annihilate runs on every exit path.
//
//   TempDir t;                       // Arrange: made + auto-annihilated
//   write_file(t / "app.ts", "...");
//   auto f = rules_scan_dir(rules, t.path());   // Act
//   CHECK(has_rule(f, "X"));                     // Assert — leak-free even if it REQUIREs
//                                                // Annihilate: t's dtor, guaranteed
class TempDir {
  public:
    TempDir() : dir_(create_temp_dir()) {}
    ~TempDir() { if (!dir_.empty()) cleanup_dir(dir_); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::string& path() const { return dir_; }
    bool valid() const { return !dir_.empty(); }
    // Join a child path, e.g. `td / "package.json"`.
    std::string operator/(const std::string& child) const { return dir_ + "/" + child; }
    operator const std::string&() const { return dir_; }  // pass where a dir string is wanted

  private:
    std::string dir_;
};

}  // namespace keur::test

#endif  // KEUR_TESTS_TEST_HELPERS_H
