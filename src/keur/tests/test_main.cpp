// keur:ignore-file SEC-041 — test support
/**
 * @file tests/test_main.cpp
 * @brief The single doctest entry point for keur's test binary.
 *
 * The test suites are split across several translation units (unit, integration,
 * perf, layer45, …) that all link into one `keur-test` binary. doctest's main
 * may be generated in exactly ONE TU — this one. Every other test TU includes
 * only `vendor/doctest.h` (no IMPLEMENT macro) and registers its SCENARIOs into
 * the shared registry at static-init time.
 *
 * @see docs/adr/ADR-002-single-cpp-binary.md
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../../../vendor/doctest.h"
