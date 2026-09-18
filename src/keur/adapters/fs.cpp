/**
 * @file adapters/fs.cpp
 * @brief Filesystem adapter implementation — the only place keur performs
 *        mutating filesystem operations. The OS-divergent primitives (mkdir -p,
 *        existence, executable write, remove, path separator) come from
 *        platform::, so this file has no POSIX headers or hardcoded separators.
 */
#include "adapters/fs.h"

#include <string>

#include "common/platform.h"

namespace keur::adapters {

bool Fs::default_exists(const std::string& path) {
    return platform::path_exists(path);
}

bool Fs::default_mkdir_p(const std::string& path) {
    return platform::make_dir(path);
}

bool Fs::default_write_executable(const std::string& path,
                                  const std::string& content) {
    return platform::write_executable(path, content);
}

void Fs::default_remove(const std::string& path) {
    platform::remove_file(path);
}

}  // namespace keur::adapters
