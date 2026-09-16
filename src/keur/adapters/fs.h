/**
 * @file adapters/fs.h
 * @brief Filesystem adapter — the single touch point for file I/O side effects.
 *
 * Every filesystem mutation (mkdir, write, chmod, remove) and the queries that
 * guard them (exists) go through this adapter. Centralising it means:
 *   - one implementation of each operation (one quoting/permission policy),
 *   - a stable seam for tests (inject a fake Fs — no real files touched),
 *   - no ad-hoc mkdir/open/unlink scattered across subcommands.
 *
 * Read-only content reads used by the rule engine stay in the engine (hot path,
 * performance-sensitive). This adapter covers the *mutating* edge + existence
 * checks used by orchestrators (hook install/remove).
 *
 * @see docs/adr/ADR-003-provider-architecture.md
 */
#pragma once
#include <functional>
#include <string>

namespace keur::adapters {

/**
 * Filesystem operations used by keur orchestrators. Implemented once in fs.cpp
 * over POSIX; tests substitute fakes by assigning the function members.
 */
struct Fs {
    /** True if a path exists (file or directory). */
    std::function<bool(const std::string& path)> exists = default_exists;

    /** Create a directory and any missing parents. Returns true on success. */
    std::function<bool(const std::string& path)> mkdir_p = default_mkdir_p;

    /** Write content to a file and mark it executable (0755). */
    std::function<bool(const std::string& path, const std::string& content)>
        write_executable = default_write_executable;

    /** Remove a file (no error if absent). */
    std::function<void(const std::string& path)> remove = default_remove;

    // Real implementations (POSIX).
    static bool default_exists(const std::string& path);
    static bool default_mkdir_p(const std::string& path);
    static bool default_write_executable(const std::string& path,
                                         const std::string& content);
    static void default_remove(const std::string& path);
};

}  // namespace keur::adapters
