/**
 * @file learn/monitor.h
 * @brief keur learn — the TUI monitor (rendering only).
 *
 * One responsibility: read state (history, supervisor status, rule inventory,
 * resources) and render a responsive full-screen frame. No orchestration, no
 * process spawning.
 */
#pragma once

#include <string>

namespace keur::learn {

/** Supervisor run status, read from eval/.run. */
struct Supervisor {
  bool running = false;
  std::string phase;
  std::string last_log;
};

Supervisor read_supervisor(const std::string& home);

/** Render one full-screen monitor frame to stdout (clears screen). */
void draw_frame(const std::string& home);

}  // namespace keur::learn
