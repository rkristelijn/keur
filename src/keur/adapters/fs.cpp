/**
 * @file adapters/fs.cpp
 * @brief Filesystem adapter implementation — the only place keur performs
 *        mutating filesystem operations. The OS-divergent primitives (mkdir -p,
 *        existence, executable write, remove, path separator) come from
 *        platform::, so this file has no POSIX headers or hardcoded separators.
 */
#include "adapters/fs.h"

#include <fstream>
#include <sstream>
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

// Plain (non-executable) write. std::ofstream is portable STL; keeping it here
// in the adapter (the sanctioned I/O edge) means the operation stays spoofable
// for tests and no orchestrator opens a stream directly. @implements ADR-003
bool Fs::default_write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << content;
    return f.good();
}

// Read a whole file into a string. A query: no side effect. @implements ADR-003
ReadResult Fs::default_read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {false, ""};
    std::ostringstream ss;
    ss << f.rdbuf();
    return {true, ss.str()};
}

void Fs::default_remove(const std::string& path) {
    platform::remove_file(path);
}

}  // namespace keur::adapters
