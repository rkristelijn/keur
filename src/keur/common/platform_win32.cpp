/**
 * @file common/platform_win32.cpp
 * @brief Platform implementation for Windows (Win32).
 *
 * No #ifdef guards needed — this file is only compiled on Windows (the Makefile
 * selects it via PLATFORM_SRC when OS == Windows_NT).
 *
 * @see docs/adr/ADR-005-platform-abstraction.md
 */
#include <windows.h>

#include <cstdio>
#include <string>

#include "common/platform.h"
#include "common/compat.h"

namespace keur::platform {

OsKind os_kind() { return OsKind::Windows; }

char path_sep() { return '\\'; }

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '\\' || a.back() == '/') return a + b;
    return a + '\\' + b;
}

int wait_exit(int raw_status) {
    // system()/_pclose() return the child's exit code directly on Windows.
    return raw_status;
}

// run/capture/capture_bytes intentionally use the shell: callers pass fixed,
// internal command strings that rely on shell features and NEVER attacker-
// controlled input. The injectable surface was removed from tool_on_path below.
// Documented, accepted exception. @see KEUR-SEC-005, ADR-005.
int run(const std::string& cmd) {
    return wait_exit(std::system(cmd.c_str()));  // keur:ignore KEUR-SEC-005  NOLINT(cert-env33-c)
}

CaptureResult capture(const std::string& cmd) {
    CaptureResult r;
    std::string full = cmd + " 2>nul";
    FILE* p = _popen(full.c_str(), "r");  // keur:ignore KEUR-SEC-005 — internal, fixed command
    if (!p) { r.exit_code = -1; return r; }
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) r.out += buf;
    r.exit_code = wait_exit(_pclose(p));
    return r;
}

int capture_bytes(const std::string& cmd, std::string& sink) {
    std::string full = cmd + " 2>nul";
    FILE* p = _popen(full.c_str(), "r");  // keur:ignore KEUR-SEC-005 — internal, fixed command
    if (!p) return -1;
    int c;
    while ((c = fgetc(p)) != EOF) sink += static_cast<char>(c);
    return wait_exit(_pclose(p));
}

bool tool_on_path(const std::string& tool) {
    // Shell-free PATH resolution via the Win32 SearchPath API — no system()/
    // _popen and no string interpolation, so a tool name cannot break out into
    // cmd (CWE-78). Tries the bare name and the common executable extensions.
    // @see ADR-005, KEUR-SEC-005.
    if (tool.empty()) return false;
    const char* exts[] = {"", ".exe", ".bat", ".cmd", ".com"};
    for (const char* ext : exts) {
        std::string name = tool + ext;
        char found[MAX_PATH];
        if (SearchPathA(nullptr, name.c_str(), nullptr, MAX_PATH, found, nullptr) > 0)
            return true;
    }
    return false;
}

bool make_dir(const std::string& path) {
    if (path.empty()) return false;
    // Create each segment in turn; both '/' and '\\' act as separators.
    std::string acc;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            // Skip empty, ".", and a bare drive spec like "C:".
            if (!acc.empty() && acc != "." &&
                !(acc.size() == 2 && acc[1] == ':')) {
                if (!CreateDirectoryA(acc.c_str(), nullptr) &&
                    GetLastError() != ERROR_ALREADY_EXISTS)
                    return false;
            }
        }
        if (i < path.size()) acc += path[i];
    }
    return true;
}

bool path_exists(const std::string& path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool write_executable(const std::string& path, const std::string& content) {
    // No POSIX executable bit on Windows; executability is extension-based.
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return true;
}

void remove_file(const std::string& path) { DeleteFileA(path.c_str()); }

Resources read_resources() {
    Resources r;
    // Windows exposes no OS load average; load1 stays 0.
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    r.ncpu = static_cast<int>(si.dwNumberOfProcessors);
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        r.ram_total_gb = (double)ms.ullTotalPhys / 1e9;
        r.ram_free_gb = (double)ms.ullAvailPhys / 1e9;
    }
    return r;
}

void sleep_ms(int ms) {
    if (ms <= 0) return;
    Sleep(static_cast<DWORD>(ms));
}

TermSize terminal_size() {
    TermSize t;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi)) {
        t.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        t.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    return t;
}

bool process_alive(long pid) {
    if (pid <= 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

}  // namespace keur::platform
