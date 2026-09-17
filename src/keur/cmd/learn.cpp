/**
 * @file cmd/learn.cpp
 * @brief keur learn — background optimise loop + responsive foreground monitor.
 */
#include "cmd/learn.h"

#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "render/box.h"
#include "render/display_width.h"
#include "adapters/proc.h"

namespace keur::cmd {

namespace {

// --- tiny JSON field extractors (the records have a known flat shape) -------
std::string json_str(const std::string& line, const std::string& key) {
  auto pos = line.find("\"" + key + "\"");
  if (pos == std::string::npos) return "";
  pos = line.find(':', pos);
  if (pos == std::string::npos) return "";
  ++pos;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '"')) ++pos;
  std::string out;
  while (pos < line.size() && line[pos] != '"' && line[pos] != ',' &&
         line[pos] != '}') {
    out += line[pos++];
  }
  // trim trailing space
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}
double json_num(const std::string& line, const std::string& key) {
  std::string s = json_str(line, key);
  try {
    return s.empty() ? 0.0 : std::stod(s);
  } catch (...) {
    return 0.0;
  }
}

// --- terminal size ----------------------------------------------------------
struct Size {
  int cols;
  int rows;
};
Size term_size() {
  struct winsize ws {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    return {static_cast<int>(ws.ws_col), static_cast<int>(ws.ws_row)};
  }
  const char* c = std::getenv("COLUMNS");
  const char* r = std::getenv("LINES");
  return {c ? std::atoi(c) : 80, r ? std::atoi(r) : 24};
}

std::string keur_home() {
  if (const char* h = std::getenv("KEUR_HOME")) return h;
  return ".";
}

// --- supervisor status (reads eval/.run) ------------------------------------
struct Supervisor {
  bool running = false;
  std::string phase;
  std::string last_log;
};
Supervisor read_supervisor(const std::string& home) {
  Supervisor s;
  std::ifstream pf(home + "/eval/.run/supervisor.pid");
  if (pf) {
    int pid = 0;
    pf >> pid;
    if (pid > 0 && kill(pid, 0) == 0) s.running = true;
  }
  std::ifstream ph(home + "/eval/.run/phase");
  if (ph) std::getline(ph, s.phase);
  // last non-empty log line
  std::ifstream lg(home + "/eval/.run/supervisor.log");
  std::string ln;
  while (std::getline(lg, ln)) {
    if (!ln.empty()) s.last_log = ln;
  }
  return s;
}

std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop = true; }

// --- aggregate one source's records into a renderable block -----------------
struct Agg {
  std::vector<double> recalls;
  std::vector<double> precs;
  std::vector<IterRecord> accepts;
  std::vector<IterRecord> all;
  std::map<std::string, int> reject_reasons;
  int n_accept = 0, n_reject = 0;
  double best_recall = 0.0, cur_recall = 0.0;
};

std::string reason_of(const std::string& note) {
  if (note.find("overfit") != std::string::npos) return "overfit";
  if (note.find("holdout") != std::string::npos) return "holdout";
  if (note.find("no-rule") != std::string::npos) return "no-rule";
  if (note.find("gate") != std::string::npos) return "gate";
  return "other";
}

Agg aggregate(const std::vector<IterRecord>& recs) {
  Agg a;
  for (const auto& r : recs) {
    a.all.push_back(r);
    a.recalls.push_back(r.recall);
    a.precs.push_back(r.precision);
    a.best_recall = std::max(a.best_recall, r.recall);
    a.cur_recall = r.recall;
    if (r.decision == "accept") {
      ++a.n_accept;
      a.accepts.push_back(r);
    } else {
      ++a.n_reject;
      a.reject_reasons[reason_of(r.note)]++;
    }
  }
  return a;
}

// --- render one source column at a given inner width ------------------------
std::vector<std::string> render_source(const std::string& name, const Agg& a,
                                        int inner, int max_decisions,
                                        bool show_bars) {
  using namespace render;
  std::vector<std::string> body;
  char buf[256];

  int total = a.n_accept + a.n_reject;
  double rate = total ? (100.0 * a.n_accept / total) : 0.0;
  std::snprintf(buf, sizeof(buf), "iters %d   accept %d   reject %d   %.0f%%",
                total, a.n_accept, a.n_reject, rate);
  body.emplace_back(buf);
  std::snprintf(buf, sizeof(buf), "recall  cur %.3f   best %.3f", a.cur_recall,
                a.best_recall);
  body.emplace_back(buf);

  // recall sparkline
  if (!a.recalls.empty()) {
    std::string sp = sparkline(a.recalls);
    std::snprintf(buf, sizeof(buf), "%.2f", a.recalls.front());
    std::string lo = buf;
    std::snprintf(buf, sizeof(buf), "%.2f", a.recalls.back());
    body.emplace_back("recall " + sp + "  " + lo + "->" + buf);
  }
  if (!a.precs.empty()) {
    body.emplace_back("prec   " + sparkline(a.precs));
  }

  if (show_bars && !a.accepts.empty()) {
    body.emplace_back("");
    body.emplace_back("accepted steps");
    for (const auto& r : a.accepts) {
      int bw = std::max(6, std::min(24, inner - 18));
      std::snprintf(buf, sizeof(buf), "i%-3d %s %.3f", r.iter,
                    block_bar(r.recall, bw).c_str(), r.recall);
      body.emplace_back(buf);
    }
  }

  if (show_bars && !a.reject_reasons.empty()) {
    body.emplace_back("");
    body.emplace_back("reject reasons");
    int tot = a.n_reject ? a.n_reject : 1;
    for (const auto& [k, v] : a.reject_reasons) {
      int bw = std::max(6, std::min(16, inner - 16));
      std::snprintf(buf, sizeof(buf), "%-8s %s %d", k.c_str(),
                    block_bar(static_cast<double>(v) / tot, bw).c_str(), v);
      body.emplace_back(buf);
    }
  }

  // last decisions
  body.emplace_back("");
  body.emplace_back("last decisions");
  int shown = 0;
  for (auto it = a.all.rbegin(); it != a.all.rend() && shown < max_decisions;
       ++it, ++shown) {
    const char* mark = (it->decision == "accept") ? "+" : "-";
    std::snprintf(buf, sizeof(buf), "%s i%-3d r=%.3f p=%.3f %s", mark, it->iter,
                  it->recall, it->precision, it->note.c_str());
    body.emplace_back(buf);
  }

  return box(name, body, inner, render::BoxStyle::Rounded);
}

}  // namespace

// --- history parser (public) ------------------------------------------------
std::vector<IterRecord> parse_history(const std::string& path) {
  std::vector<IterRecord> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.find("\"iter\"") == std::string::npos) continue;
    IterRecord r;
    r.iter = static_cast<int>(json_num(line, "iter"));
    r.ts = json_str(line, "ts");
    r.decision = json_str(line, "decision");
    r.recall = json_num(line, "recall");
    r.precision = json_num(line, "precision");
    r.note = json_str(line, "note");
    r.source = json_str(line, "source");
    if (r.source.empty()) r.source = "corpus";
    out.push_back(r);
  }
  return out;
}

// --- option parsing ---------------------------------------------------------
LearnOptions learn_parse(const std::vector<std::string>& args) {
  LearnOptions o;
  for (size_t i = 0; i < args.size(); ++i) {
    const auto& a = args[i];
    if (a == "--stop") o.stop = true;
    else if (a == "--status") o.status = true;
    else if (a == "--no-spin") o.no_spin = true;
    else if (a == "--source" && i + 1 < args.size()) o.source = args[++i];
    else if (a == "--model" && i + 1 < args.size()) o.model = args[++i];
    else if (a == "--refresh" && i + 1 < args.size()) o.refresh_secs = std::atoi(args[++i].c_str());
  }
  return o;
}

// --- rule inventory on disk (for the plain-terms summary) -------------------
struct RuleStats {
  std::map<std::string, int> learned_by_cat;   // category -> count in rules/learned
  int learned_total = 0;
  int composite_total = 0;                      // slop kept as composite building blocks
};

// count `category: X` lines in .rule files under a directory (non-recursive is
// fine here; learned/ and composite-candidates/ are flat).
static RuleStats read_rule_stats(const std::string& home) {
  namespace fs = std::filesystem;
  RuleStats s;
  std::error_code ec;
  auto count_dir = [&](const std::string& dir, bool into_cat) {
    fs::path p(dir);
    if (!fs::is_directory(p, ec)) return;
    for (auto& e : fs::directory_iterator(p, ec)) {
      if (e.path().extension() != ".rule") continue;
      std::ifstream f(e.path());
      std::string line, cat;
      while (std::getline(f, line)) {
        if (line.rfind("category:", 0) == 0) {
          cat = line.substr(9);
          // trim
          size_t a = cat.find_first_not_of(" \t");
          if (a != std::string::npos) cat = cat.substr(a);
          break;
        }
      }
      if (into_cat) {
        s.learned_by_cat[cat.empty() ? "(uncat)" : cat]++;
        s.learned_total++;
      } else {
        s.composite_total++;
      }
    }
  };
  count_dir(home + "/rules/learned", true);
  count_dir(home + "/eval/composite-candidates", false);
  return s;
}

// --- machine resources + run duration (for the plain-terms summary) ---------
struct Resources {
  double load1 = 0.0;       // 1-min load average
  int ncpu = 0;             // logical cpus
  double ram_total_gb = 0;
  double ram_free_gb = 0;   // free + inactive (macOS: practically available)
  long run_secs = 0;        // how long the current run has been going
};

static Resources read_resources(const std::string& home) {
  Resources r;
  size_t sz = 0;
  if (sysctlbyname("vm.loadavg", nullptr, nullptr, nullptr, 0) == 0) {
    struct loadavg lv {};
    sz = sizeof(lv);
    if (sysctlbyname("vm.loadavg", &lv, &sz, nullptr, 0) == 0)
      r.load1 = (double)lv.ldavg[0] / (double)lv.fscale;
  }
  { int n = 0; sz = sizeof(n);
    if (sysctlbyname("hw.logicalcpu", &n, &sz, nullptr, 0) == 0) r.ncpu = n; }
  { uint64_t m = 0; sz = sizeof(m);
    if (sysctlbyname("hw.memsize", &m, &sz, nullptr, 0) == 0)
      r.ram_total_gb = (double)m / 1e9; }
#if defined(__APPLE__)
  { vm_size_t page = 0; host_page_size(mach_host_self(), &page);
    vm_statistics64_data_t vm {};
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &cnt) == KERN_SUCCESS) {
      r.ram_free_gb = (double)((vm.free_count + vm.inactive_count) * page) / 1e9;
    } }
#endif
  // run duration from the supervisor pidfile mtime (set when the run started)
  namespace fs = std::filesystem;
  std::error_code ec;
  auto pidf = fs::path(home) / "eval/.run/supervisor.pid";
  if (fs::exists(pidf, ec)) {
    auto ft = fs::last_write_time(pidf, ec);
    if (!ec) {
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
      auto now = std::chrono::system_clock::now();
      r.run_secs = std::chrono::duration_cast<std::chrono::seconds>(now - sctp).count();
      if (r.run_secs < 0) r.run_secs = 0;
    }
  }
  return r;
}

static std::string fmt_duration(long s) {
  long h = s / 3600, m = (s % 3600) / 60;
  char buf[32];
  if (h > 0) std::snprintf(buf, sizeof(buf), "%ldh %ldm", h, m);
  else       std::snprintf(buf, sizeof(buf), "%ldm", m);
  return buf;
}

// --- one full-screen frame --------------------------------------------------
static void draw_frame(const std::string& home) {
  auto recs = parse_history(home + "/eval/history/optimiser.jsonl");
  auto sup = read_supervisor(home);
  Size sz = term_size();
  int inner_total = std::max(40, sz.cols - 2);

  // split records per source
  std::vector<IterRecord> corpus, keureval;
  for (const auto& r : recs) {
    (r.source == "keureval" ? keureval : corpus).push_back(r);
  }

  std::cout << "\033[2J\033[H";  // clear + home
  std::string status = sup.running ? "RUNNING" : "stopped";
  std::string head = "keur learn · loop " + status;
  if (!sup.phase.empty()) head += " · phase " + sup.phase;
  std::cout << head << "\n";

  // --- plain-terms summary (no jargon) --------------------------------------
  // Explains, for a human: how many rules keur has learned, in which areas, how
  // many are still weak building blocks, and how often a training attempt
  // actually produces a usable rule (the "success rate").
  {
    RuleStats rs = read_rule_stats(home);
    // success rate = accepts / total attempts, over ALL history (both sources).
    int acc = 0, tot = (int)recs.size();
    for (const auto& r : recs) if (r.decision == "accept") ++acc;
    int pct = tot ? (int)(100.0 * acc / tot + 0.5) : 0;

    std::cout << "in plain terms:\n";
    std::cout << "  rules keur has learned : " << rs.learned_total
              << "   (weak building blocks kept aside: " << rs.composite_total << ")\n";
    // per-area line, compact
    std::string areas;
    for (const auto& [cat, n] : rs.learned_by_cat) {
      if (!areas.empty()) areas += "  ";
      areas += cat + " " + std::to_string(n);
    }
    if (!areas.empty())
      std::cout << "  by area                : " << render::truncate_to(areas, inner_total - 27) << "\n";
    std::cout << "  learning success rate  : " << pct << "%  (" << acc
              << " keepers out of " << tot << " tries — the rest were rejected as "
                 "too weak/noisy)\n";
    // machine resources + how long this run has been going
    Resources rc = read_resources(home);
    char rbuf[256];
    double load_pct = rc.ncpu ? (100.0 * rc.load1 / rc.ncpu) : 0.0;
    std::snprintf(rbuf, sizeof(rbuf),
                  "  running for            : %s   CPU load %.0f%% of %d cores   "
                  "RAM free %.1f/%.0f GB",
                  fmt_duration(rc.run_secs).c_str(), load_pct, rc.ncpu,
                  rc.ram_free_gb, rc.ram_total_gb);
    std::cout << rbuf << "\n";
    std::cout << "\n";
  }

  bool wide = sz.cols >= 120;
  bool std_w = sz.cols >= 80;
  int max_dec = std::max(3, std::min(8, sz.rows - 14));

  if (wide && !corpus.empty() && !keureval.empty()) {
    int col = (inner_total - 4) / 2;
    auto l = render_source("corpus", aggregate(corpus), col, max_dec, true);
    auto r = render_source("keureval", aggregate(keureval), col, max_dec, true);
    for (const auto& ln : render::beside(l, r, 2)) std::cout << ln << "\n";
  } else {
    // single column: show whichever source has data (prefer the active one)
    const auto& src = !keureval.empty() && corpus.empty() ? keureval : corpus;
    std::string name = (&src == &keureval) ? "keureval" : "corpus";
    auto blk = render_source(name, aggregate(src.empty() ? recs : src),
                             inner_total, max_dec, std_w);
    for (const auto& ln : blk) std::cout << ln << "\n";
    // if both exist but terminal is narrow, show the other compactly below
    const auto& other = (&src == &corpus) ? keureval : corpus;
    if (!other.empty() && std_w) {
      std::string oname = (&other == &keureval) ? "keureval" : "corpus";
      auto ob = render_source(oname, aggregate(other), inner_total,
                              std::min(3, max_dec), false);
      for (const auto& ln : ob) std::cout << ln << "\n";
    }
  }

  if (!sup.last_log.empty()) {
    std::cout << render::truncate_to("log: " + sup.last_log, inner_total) << "\n";
  }
  std::cout << "(Ctrl+C leaves monitor; loop keeps running · keur learn --stop)\n";
  std::cout.flush();
}

// --- orchestration ----------------------------------------------------------
int learn(const LearnOptions& opts) {
  const std::string home = keur_home();
  // All external side effects go through the Proc adapter (ADR-003 / KEUR-ARCH-001):
  // no raw process spawning in a cmd/ orchestrator.
  adapters::Proc proc;

  if (opts.stop) {
    return proc.run("'" + home + "/eval/supervise.sh' stop");
  }

  if (opts.status) {
    draw_frame(home);
    return 0;
  }

  // spin the background loop if nothing is running and not told to skip
  Supervisor sup = read_supervisor(home);
  if (!sup.running && !opts.no_spin) {
    // MODEL is honoured by optimise.sh (env override); pass it only when set so
    // an empty --model falls back to optimise.sh's own default selection.
    std::string model_env = opts.model.empty() ? "" : ("MODEL='" + opts.model + "' ");
    std::string cmd = model_env + "KEUR_EVAL_SOURCE=" + opts.source +
                      " KEUR_LOOP_CMD='" + home + "/eval/optimise.sh iterate 500' '" +
                      home + "/eval/supervise.sh' start";
    std::cerr << "keur learn: spinning background loop (source=" << opts.source;
    if (!opts.model.empty()) std::cerr << ", model=" << opts.model;
    std::cerr << ")...\n";
    proc.run(cmd);
  }

  // foreground monitor loop
  std::signal(SIGINT, on_sigint);
  while (!g_stop) {
    draw_frame(home);
    for (int i = 0; i < opts.refresh_secs * 10 && !g_stop; ++i) {
      usleep(100000);  // 100ms slices so Ctrl+C is responsive
    }
  }
  std::cout << "\nmonitor closed. loop still running in background "
               "(keur learn --stop to end it).\n";
  return 0;
}

}  // namespace keur::cmd
