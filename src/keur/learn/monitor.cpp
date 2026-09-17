/**
 * @file learn/monitor.cpp
 * @brief keur learn TUI — reads state, renders a responsive frame.
 */
#include "learn/monitor.h"

#include <sys/ioctl.h>
#include <unistd.h>
#include <csignal>  // kill()

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "learn/history.h"
#include "learn/sysinfo.h"
#include "render/box.h"
#include "render/display_width.h"

namespace keur::learn {

namespace {

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

// --- rule inventory on disk (for the plain-terms summary) -------------------
struct RuleStats {
  std::map<std::string, int> learned_by_cat;
  int learned_total = 0;
  int composite_total = 0;
};

RuleStats read_rule_stats(const std::string& home) {
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
          size_t a = cat.find_first_not_of(" \t");
          if (a != std::string::npos) cat = cat.substr(a);
          break;
        }
      }
      if (into_cat) { s.learned_by_cat[cat.empty() ? "(uncat)" : cat]++; s.learned_total++; }
      else          { s.composite_total++; }
    }
  };
  count_dir(home + "/rules/learned", true);
  count_dir(home + "/eval/composite-candidates", false);
  return s;
}

// --- render one source column at a given inner width ------------------------
std::vector<std::string> render_source(const std::string& name, const Agg& a,
                                       int inner, int max_decisions, bool show_bars) {
  using namespace render;
  std::vector<std::string> body;
  char buf[256];

  int total = a.n_accept + a.n_reject;
  double rate = total ? (100.0 * a.n_accept / total) : 0.0;
  std::snprintf(buf, sizeof(buf), "iters %d   accept %d   reject %d   %.0f%%",
                total, a.n_accept, a.n_reject, rate);
  body.emplace_back(buf);
  std::snprintf(buf, sizeof(buf), "recall  cur %.3f   best %.3f", a.cur_recall, a.best_recall);
  body.emplace_back(buf);

  if (!a.recalls.empty()) {
    std::string sp = sparkline(a.recalls);
    std::snprintf(buf, sizeof(buf), "%.2f", a.recalls.front());
    std::string lo = buf;
    std::snprintf(buf, sizeof(buf), "%.2f", a.recalls.back());
    body.emplace_back("recall " + sp + "  " + lo + "->" + buf);
  }
  if (!a.precs.empty()) body.emplace_back("prec   " + sparkline(a.precs));

  if (show_bars && !a.accepts.empty()) {
    body.emplace_back("");
    body.emplace_back("accepted steps");
    for (const auto& r : a.accepts) {
      int bw = std::max(6, std::min(24, inner - 18));
      std::snprintf(buf, sizeof(buf), "i%-3d %s %.3f", r.iter, block_bar(r.recall, bw).c_str(), r.recall);
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

  body.emplace_back("");
  body.emplace_back("last decisions");
  int shown = 0;
  for (auto it = a.all.rbegin(); it != a.all.rend() && shown < max_decisions; ++it, ++shown) {
    const char* mark = (it->decision == "accept") ? "+" : "-";
    std::snprintf(buf, sizeof(buf), "%s i%-3d r=%.3f p=%.3f %s", mark, it->iter,
                  it->recall, it->precision, it->note.c_str());
    body.emplace_back(buf);
  }
  return box(name, body, inner, render::BoxStyle::Rounded);
}

}  // namespace

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
  std::ifstream lg(home + "/eval/.run/supervisor.log");
  std::string ln;
  while (std::getline(lg, ln)) if (!ln.empty()) s.last_log = ln;
  return s;
}

void draw_frame(const std::string& home) {
  auto recs = parse_history(home + "/eval/history/optimiser.jsonl");
  auto sup = read_supervisor(home);
  Size sz = term_size();
  int inner_total = std::max(40, sz.cols - 2);

  std::vector<IterRecord> corpus, keureval;
  for (const auto& r : recs) (r.source == "keureval" ? keureval : corpus).push_back(r);

  std::cout << "\033[2J\033[H";  // clear + home
  std::string status = sup.running ? "RUNNING" : "stopped";
  std::string head = "keur learn · loop " + status;
  if (!sup.phase.empty()) head += " · phase " + sup.phase;
  std::cout << head << "\n";

  // plain-terms summary (jargon-free)
  {
    RuleStats rs = read_rule_stats(home);
    int acc = 0, tot = (int)recs.size();
    for (const auto& r : recs) if (r.decision == "accept") ++acc;
    int pct = tot ? (int)(100.0 * acc / tot + 0.5) : 0;

    std::cout << "in plain terms:\n";
    std::cout << "  rules keur has learned : " << rs.learned_total
              << "   (weak building blocks kept aside: " << rs.composite_total << ")\n";
    std::string areas;
    for (const auto& [cat, n] : rs.learned_by_cat) {
      if (!areas.empty()) areas += "  ";
      areas += cat + " " + std::to_string(n);
    }
    if (!areas.empty())
      std::cout << "  by area                : " << render::truncate_to(areas, inner_total - 27) << "\n";
    std::cout << "  learning success rate  : " << pct << "%  (" << acc
              << " keepers out of " << tot << " tries — the rest were rejected as too weak/noisy)\n";
    Resources rc = read_resources(home);
    char rbuf[256];
    double load_pct = rc.ncpu ? (100.0 * rc.load1 / rc.ncpu) : 0.0;
    std::snprintf(rbuf, sizeof(rbuf),
                  "  running for            : %s   CPU load %.0f%% of %d cores   RAM free %.1f/%.0f GB",
                  fmt_duration(rc.run_secs).c_str(), load_pct, rc.ncpu, rc.ram_free_gb, rc.ram_total_gb);
    std::cout << rbuf << "\n\n";
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
    const auto& src = !keureval.empty() && corpus.empty() ? keureval : corpus;
    std::string name = (&src == &keureval) ? "keureval" : "corpus";
    auto blk = render_source(name, aggregate(src.empty() ? recs : src), inner_total, max_dec, std_w);
    for (const auto& ln : blk) std::cout << ln << "\n";
    const auto& other = (&src == &corpus) ? keureval : corpus;
    if (!other.empty() && std_w) {
      std::string oname = (&other == &keureval) ? "keureval" : "corpus";
      auto ob = render_source(oname, aggregate(other), inner_total, std::min(3, max_dec), false);
      for (const auto& ln : ob) std::cout << ln << "\n";
    }
  }

  if (!sup.last_log.empty())
    std::cout << render::truncate_to("log: " + sup.last_log, inner_total) << "\n";
  std::cout << "(Ctrl+C leaves monitor; loop keeps running · keur learn --stop)\n";
  std::cout.flush();
}

}  // namespace keur::learn
