/**
 * @file common/platform_posix.cpp
 * @brief Platform implementation for macOS and Linux (POSIX).
 *
 * The only permitted #ifdef here is __APPLE__ to distinguish macOS (mach +
 * sysctl) from Linux (/proc). All other POSIX APIs are identical across both.
 * Selected by the Makefile when OS != Windows_NT.
 *
 * @see docs/adr/ADR-005-platform-abstraction.md
 */
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <csignal>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "common/platform.h"
#include "common/compat.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

namespace keur::platform {

OsKind os_kind() {
#ifdef __APPLE__
    return OsKind::MacOS;
#else
    return OsKind::Linux;
#endif
}

char path_sep() { return '/'; }

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/') return a + b;
    return a + '/' + b;
}

int wait_exit(int raw_status) {
    if (raw_status == -1) return -1;
    if (WIFEXITED(raw_status)) return WEXITSTATUS(raw_status);
    return 1;
}

// run/capture/capture_bytes intentionally use the shell: callers pass fixed,
// internal command strings that rely on shell features (git argument splitting,
// env-var prefixes, redirects) and NEVER attacker-controlled input. The command
// injection surface (a bare, possibly-influenced tool name) was removed from
// tool_on_path below. These three are a documented, accepted exception.
// @see KEUR-SEC-005, ADR-005.
int run(const std::string& cmd) {
    return wait_exit(std::system(cmd.c_str()));  // keur:ignore KEUR-SEC-005  NOLINT(cert-env33-c)
}

CaptureResult capture(const std::string& cmd) {
    CaptureResult r;
    std::string full = cmd + " 2>/dev/null";
    FILE* p = popen(full.c_str(), "r");  // keur:ignore KEUR-SEC-005 — internal, fixed command
    if (!p) { r.exit_code = -1; return r; }
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) r.out += buf;
    r.exit_code = wait_exit(pclose(p));
    return r;
}

int capture_bytes(const std::string& cmd, std::string& sink) {
    std::string full = cmd + " 2>/dev/null";
    FILE* p = popen(full.c_str(), "r");  // keur:ignore KEUR-SEC-005 — internal, fixed command
    if (!p) return -1;
    int c;
    while ((c = fgetc(p)) != EOF) sink += static_cast<char>(c);
    return wait_exit(pclose(p));
}

bool tool_on_path(const std::string& tool) {
    // Shell-free PATH resolution: no system()/popen and no string interpolation,
    // so a tool name can never break out into a shell (CWE-78). A name with a
    // separator is checked directly; a bare name is searched across $PATH.
    // @see ADR-005, KEUR-SEC-005.
    if (tool.empty()) return false;
    auto is_exec = [](const std::string& p) { return access(p.c_str(), X_OK) == 0; };
    if (tool.find('/') != std::string::npos) return is_exec(tool);
    const char* penv = getenv("PATH");
    if (!penv) return false;
    const std::string paths = penv;
    size_t start = 0;
    while (start <= paths.size()) {
        size_t colon = paths.find(':', start);
        std::string dir = paths.substr(
            start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!dir.empty() && is_exec(path_join(dir, tool))) return true;
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return false;
}

bool make_dir(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    auto sep = path.rfind('/');
    if (sep != std::string::npos && sep > 0) {
        if (!make_dir(path.substr(0, sep))) return false;
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

bool path_exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

bool write_executable(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) return false;
    f << content;
    f.close();
    chmod(path.c_str(), 0755);
    return true;
}

void remove_file(const std::string& path) { unlink(path.c_str()); }

Resources read_resources() {
    Resources r;
#ifdef __APPLE__
    size_t sz = 0;
    { struct loadavg lv{};
      sz = sizeof(lv);
      if (sysctlbyname("vm.loadavg", &lv, &sz, nullptr, 0) == 0)
        r.load1 = (double)lv.ldavg[0] / (double)lv.fscale; }
    { int n = 0; sz = sizeof(n);
      if (sysctlbyname("hw.logicalcpu", &n, &sz, nullptr, 0) == 0) r.ncpu = n; }
    { uint64_t m = 0; sz = sizeof(m);
      if (sysctlbyname("hw.memsize", &m, &sz, nullptr, 0) == 0)
        r.ram_total_gb = (double)m / 1e9; }
    { vm_size_t page = 0; host_page_size(mach_host_self(), &page);
      vm_statistics64_data_t vm{};
      mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
      if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                            (host_info64_t)&vm, &cnt) == KERN_SUCCESS) {
        r.ram_free_gb = (double)((vm.free_count + vm.inactive_count) * page) / 1e9;
      } }
#else
    if (FILE* f = std::fopen("/proc/loadavg", "r")) {
        double l1 = 0.0;
        if (std::fscanf(f, "%lf", &l1) == 1) r.load1 = l1;
        std::fclose(f);
    }
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) r.ncpu = static_cast<int>(n);
    // Parse /proc/meminfo for MemTotal + MemAvailable (kB).
    if (std::ifstream mi("/proc/meminfo"); mi) {
        std::string key;
        unsigned long long kb = 0;
        std::string unit;
        uint64_t total_kb = 0, avail_kb = 0, free_kb = 0;
        while (mi >> key >> kb >> unit) {
            if (key == "MemTotal:") total_kb = kb;
            else if (key == "MemAvailable:") avail_kb = kb;
            else if (key == "MemFree:") free_kb = kb;
        }
        if (!avail_kb) avail_kb = free_kb;  // fallback on old kernels
        if (total_kb) r.ram_total_gb = (double)total_kb * 1024.0 / 1e9;
        if (avail_kb) r.ram_free_gb = (double)avail_kb * 1024.0 / 1e9;
    }
#endif
    return r;
}

void sleep_ms(int ms) {
    if (ms <= 0) return;
    usleep(static_cast<useconds_t>(ms) * 1000);
}

TermSize terminal_size() {
    TermSize t;
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        t.cols = ws.ws_col;
        t.rows = ws.ws_row;
    }
    return t;
}

bool process_alive(long pid) {
    if (pid <= 0) return false;
    return kill(static_cast<pid_t>(pid), 0) == 0;
}

}  // namespace keur::platform
