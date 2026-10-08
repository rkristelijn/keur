/**
 * @file common/platform_win32.cpp
 * @brief Platform implementation for Windows (Win32).
 *
 * No #ifdef guards needed — this file is only compiled on Windows (the Makefile
 * selects it via PLATFORM_SRC when OS == Windows_NT).
 *
 * @see docs/adr/ADR-005-platform-abstraction.md
 * @implements ADR-005
 */
#include <windows.h>

#include <cstdio>
#include <io.h>
#include <cstring>
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

int run_argv(const std::vector<std::string>& argv,
             const std::vector<std::pair<std::string, std::string>>& env) {
    if (argv.empty()) return -1;

    // Quote each argument per the Win32 CommandLineToArgvW rules so a space or
    // quote in an argument cannot split it or inject extra arguments. No shell
    // (cmd.exe) is involved, so shell metacharacters are inert (CWE-78).
    auto quote = [](const std::string& a) -> std::string {
        if (!a.empty() &&
            a.find_first_of(" \t\"") == std::string::npos)
            return a;
        std::string out = "\"";
        size_t backslashes = 0;
        for (char c : a) {
            if (c == '\\') {
                ++backslashes;
            } else if (c == '"') {
                out.append(backslashes * 2 + 1, '\\');
                out += '"';
                backslashes = 0;
            } else {
                out.append(backslashes, '\\');
                backslashes = 0;
                out += c;
            }
        }
        out.append(backslashes * 2, '\\');
        out += '"';
        return out;
    };

    std::string cmdline;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmdline += ' ';
        cmdline += quote(argv[i]);
    }

    // Build the environment block: inherited vars minus any we override, then
    // the overrides, as a double-NUL-terminated "KEY=value\0...\0" buffer.
    std::string block;
    if (!env.empty()) {
        LPCH parent = GetEnvironmentStringsA();
        if (parent) {
            for (LPCH e = parent; *e; e += std::strlen(e) + 1) {
                std::string cur = e;
                auto eq = cur.find('=');
                bool overridden = false;
                if (eq != std::string::npos) {
                    std::string key = cur.substr(0, eq);
                    for (const auto& kv : env)
                        if (kv.first == key) { overridden = true; break; }
                }
                if (!overridden) { block += cur; block += '\0'; }
            }
            FreeEnvironmentStringsA(parent);
        }
        for (const auto& kv : env) {
            block += kv.first + "=" + kv.second;
            block += '\0';
        }
        block += '\0';  // terminating NUL for the block
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Mutable buffer for the command line (CreateProcessA may modify it).
    std::vector<char> cmdbuf(cmdline.begin(), cmdline.end());
    cmdbuf.push_back('\0');

    BOOL ok = CreateProcessA(
        nullptr,               // resolve argv[0] via PATH from the command line
        cmdbuf.data(),
        nullptr, nullptr, FALSE, 0,
        env.empty() ? nullptr : block.data(),
        nullptr, &si, &pi);
    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
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

// Shell-free capture: CreateProcess with stdout redirected to a pipe and no
// cmd.exe, so argv content can never be interpreted as a shell (CWE-78).
int capture_bytes_argv(const std::vector<std::string>& argv, std::string& sink) {
    if (argv.empty()) return -1;

    // Quote each argument per CommandLineToArgvW rules (same as run_argv).
    auto quote = [](const std::string& a) -> std::string {
        if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
        std::string out = "\"";
        size_t bs = 0;
        for (char c : a) {
            if (c == '\\') { ++bs; }
            else if (c == '"') { out.append(bs * 2 + 1, '\\'); out += '"'; bs = 0; }
            else { out.append(bs, '\\'); bs = 0; out += c; }
        }
        out.append(bs * 2, '\\'); out += '"';
        return out;
    };
    std::string cmdline;
    for (size_t i = 0; i < argv.size(); ++i) { if (i) cmdline += ' '; cmdline += quote(argv[i]); }

    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);  // parent read end not inherited

    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);  // stderr passthrough
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};

    std::vector<char> cl(cmdline.begin(), cmdline.end()); cl.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, cl.data(), nullptr, nullptr, TRUE,
                             0, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);  // parent doesn't write
    if (!ok) { CloseHandle(rd); return -1; }

    char buf[512]; DWORD n = 0;
    while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0)
        sink.append(buf, n);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

CaptureResult capture_argv(const std::vector<std::string>& argv) {
    CaptureResult r;
    r.exit_code = capture_bytes_argv(argv, r.out);
    return r;
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

bool stderr_is_tty() {
    return _isatty(_fileno(stderr)) != 0;
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

// ---------------------------------------------------------------------------
// Interactive terminal input (Win32): raw mode + one-key read.
// We enable ENABLE_VIRTUAL_TERMINAL_INPUT so the console delivers the same
// "\033[A"-style sequences POSIX does — then the shared decode_key() maps them
// identically. Line input + echo are disabled for single-key reads.
// ---------------------------------------------------------------------------

RawModeGuard::RawModeGuard() {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return;  // no console
    static_assert(sizeof(DWORD) <= sizeof(saved_), "saved_ too small for DWORD");
    std::memcpy(saved_, &mode, sizeof(DWORD));

    DWORD raw = mode;
    raw &= ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    raw |= ENABLE_VIRTUAL_TERMINAL_INPUT;  // deliver VT sequences like POSIX
    if (SetConsoleMode(h, raw)) active_ = true;
}

void RawModeGuard::restore() {
    if (!active_) return;
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD prev = 0;
    std::memcpy(&prev, saved_, sizeof(DWORD));
    if (h != INVALID_HANDLE_VALUE) SetConsoleMode(h, prev);
    active_ = false;
}

RawModeGuard::~RawModeGuard() { restore(); }

RawModeGuard::RawModeGuard(RawModeGuard&& other) noexcept {
    active_ = other.active_;
    std::memcpy(saved_, other.saved_, sizeof(saved_));
    other.active_ = false;
}

RawModeGuard& RawModeGuard::operator=(RawModeGuard&& other) noexcept {
    if (this != &other) {
        restore();
        active_ = other.active_;
        std::memcpy(saved_, other.saved_, sizeof(saved_));
        other.active_ = false;
    }
    return *this;
}

KeyEvent read_key() {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return {Key::Unknown, '\0'};

    char c = 0;
    DWORD n = 0;
    if (!ReadFile(h, &c, 1, &n, nullptr) || n == 0) return {Key::Unknown, '\0'};

    std::string buf(1, c);
    if (static_cast<unsigned char>(c) == 0x1b) {
        char b1 = 0;
        if (ReadFile(h, &b1, 1, &n, nullptr) && n == 1) {
            buf.push_back(b1);
            if (b1 == '[') {
                char b2 = 0;
                if (ReadFile(h, &b2, 1, &n, nullptr) && n == 1) buf.push_back(b2);
            }
        }
    }
    return decode_key(buf);
}

}  // namespace keur::platform
