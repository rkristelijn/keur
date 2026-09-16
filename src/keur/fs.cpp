/**
 * @file fs.cpp
 * @brief Filesystem adapter implementation (POSIX) — the only place keur
 *        performs mutating filesystem operations.
 */
#include "fs.h"

#include <cerrno>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace keur {

bool Fs::default_exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

bool Fs::default_mkdir_p(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    // Create the parent first, then this level.
    auto sep = path.rfind('/');
    if (sep != std::string::npos && sep > 0) {
        if (!default_mkdir_p(path.substr(0, sep))) return false;
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

bool Fs::default_write_executable(const std::string& path,
                                  const std::string& content) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) return false;
    f << content;
    f.close();
    chmod(path.c_str(), 0755);
    return true;
}

void Fs::default_remove(const std::string& path) {
    unlink(path.c_str());
}

}  // namespace keur
