# Makefile — keur single binary + full test pipeline.
#
# Build:            make               (build bin/keur)
# Test (all):       make test          (unit + integration + e2e)
# Test tiers:       make test-unit  make test-integration  make e2e  make perf
# Coverage:         make coverage      (gcov report)
# Mutation testing: make mutate        (requires mull-runner + clang plugin)
# Clean:            make clean
#
# Layout (see ADR-003):
#   cmd/       subcommand orchestrators
#   adapters/  git / fs / proc — the only place external I/O happens
#   report/    findings output provider
#   util/      shared helpers
#
# Dependencies: RE2 + abseil (Homebrew on macOS, system packages on Linux).

CXX      ?= c++
VERSION  := $(shell grep '^version' ../../config/keur.toml 2>/dev/null | head -1 | sed 's/.*=[ ]*//; s/"//g; s/[[:space:]]//g')
VERSION  := $(or $(VERSION),dev)

# -I..     → resolves rules/rule_engine.h  (src/rules/...)
# -I.      → resolves cmd/, adapters/, report/, util/  (src/keur/...)
# -I../..  → resolves vendor/doctest.h
CXXFLAGS ?= -Wall -Wextra -std=c++20 -O2 -DKEUR_VERSION='"$(VERSION)"'
INCLUDES  = -I. -I.. -I../..

# RE2 + abseil
RE2_PREFIX  ?= $(shell brew --prefix re2  2>/dev/null || echo /usr)
ABSL_PREFIX ?= $(shell brew --prefix abseil 2>/dev/null || echo /usr)
RE2_CFLAGS   = -I$(RE2_PREFIX)/include -I$(ABSL_PREFIX)/include
RE2_LDFLAGS  = -L$(RE2_PREFIX)/lib -L$(ABSL_PREFIX)/lib -lre2

BINDIR = ../../bin
BIN    = $(BINDIR)/keur

REPO_ROOT := $(shell cd ../.. && pwd -P)

# Rule engine (shared, lower layer)
RULES_SRC = ../rules/rule_engine.cpp
RULES_OBJ = $(BINDIR)/rule_engine.o

# All keur sources (relative to src/keur)
MAIN_SRC   = main.cpp
IMPL_SRCS  = cmd/check.cpp cmd/hook.cpp cmd/scan.cpp cmd/policy.cpp \
             adapters/git.cpp adapters/fs.cpp adapters/proc.cpp report/report.cpp
# Object files: flatten path (cmd/check.cpp -> keur_cmd_check.o) via subst
IMPL_OBJS  = $(patsubst %.cpp,$(BINDIR)/keur_%.o,$(subst /,_,$(IMPL_SRCS)))
MAIN_OBJ   = $(BINDIR)/keur_main.o
TEST_OBJ   = $(BINDIR)/keur_test.o
TEST_BIN   = $(BINDIR)/keur-test

.PHONY: all build test test-unit test-integration perf e2e coverage mutate clean help
.DEFAULT_GOAL := build

help: ## Show targets
	@grep -E '^[a-zA-Z_-]+:.*## ' $(MAKEFILE_LIST) | sort | \
	  awk 'BEGIN{FS=":.*## "}{printf "  \033[36m%-18s\033[0m %s\n", $$1, $$2}'

## ---- Build ----------------------------------------------------------------

build: $(BIN) ## Build bin/keur

$(BINDIR):
	@mkdir -p $(BINDIR)

$(RULES_OBJ): $(RULES_SRC) ../rules/rule_engine.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

# Generic pattern: compile any keur source to a flattened object name.
# $* is the stem with slashes turned to underscores by the explicit rules below.
$(MAIN_OBJ): main.cpp cmd/check.h cmd/hook.h cmd/scan.h cmd/policy.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_cmd_check.o: cmd/check.cpp cmd/check.h cmd/policy.h adapters/git.h util/keur_utils.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_cmd_hook.o: cmd/hook.cpp cmd/hook.h adapters/git.h adapters/fs.h adapters/proc.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_cmd_scan.o: cmd/scan.cpp cmd/scan.h report/report.h util/keur_utils.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_cmd_policy.o: cmd/policy.cpp cmd/policy.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_adapters_git.o: adapters/git.cpp adapters/git.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_adapters_fs.o: adapters/fs.cpp adapters/fs.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_adapters_proc.o: adapters/proc.cpp adapters/proc.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_report_report.o: report/report.cpp report/report.h util/keur_utils.h ../rules/rule_engine.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BIN): $(MAIN_OBJ) $(IMPL_OBJS) $(RULES_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(RE2_LDFLAGS)
	@echo "built $(BIN) (version $(VERSION))"

## ---- Test binary ----------------------------------------------------------

$(TEST_OBJ): keur_test.cpp cmd/check.h cmd/hook.h cmd/scan.h cmd/policy.h \
             adapters/git.h adapters/fs.h adapters/proc.h report/report.h util/keur_utils.h \
             ../../vendor/doctest.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -DKEUR_TEST_ROOT='"$(REPO_ROOT)"' -c $< -o $@

$(TEST_BIN): $(TEST_OBJ) $(IMPL_OBJS) $(RULES_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(RE2_LDFLAGS)
	@echo "built $(TEST_BIN)"

## ---- Test tiers -----------------------------------------------------------

test: test-unit test-integration e2e ## Run all tests (unit + integration + e2e)

test-unit: $(TEST_BIN) ## Run unit tests only (pure functions, fast)
	@echo "--- unit ---"
	$(TEST_BIN) --test-suite=unit

test-integration: $(TEST_BIN) ## Run integration tests (real rule engine + fixtures)
	@echo "--- integration ---"
	$(TEST_BIN) --test-suite=integration

perf: $(TEST_BIN) ## Run performance timing guards
	@echo "--- perf ---"
	$(TEST_BIN) --test-suite=perf --success

e2e: build ## Run end-to-end tests (binary + git repos)
	@bash ../../tests/e2e/run-e2e.sh $(BIN)

## ---- Coverage -------------------------------------------------------------

# Coverage sources (flat list for the single-command coverage build).
COV_SRCS = keur_test.cpp cmd/check.cpp cmd/hook.cpp cmd/scan.cpp cmd/policy.cpp \
           adapters/git.cpp adapters/fs.cpp adapters/proc.cpp report/report.cpp ../rules/rule_engine.cpp

coverage: ## Build test binary with coverage and report (gcov)
	@mkdir -p ../../.tmp/cov
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) --coverage -O0 \
	  -DKEUR_TEST_ROOT='"$(REPO_ROOT)"' \
	  -o ../../.tmp/cov/keur-test-cov $(COV_SRCS) $(RE2_LDFLAGS)
	cd ../../.tmp/cov && ./keur-test-cov --test-suite-exclude=perf >/dev/null 2>&1 || true
	@echo ""
	@echo "Coverage (keur sources):"
	@cd ../../.tmp/cov && for src in cmd_check cmd_hook cmd_scan cmd_policy git fs proc report; do \
	  line=$$(gcov "keur-test-cov-$$src" 2>/dev/null | grep -A1 "File '.*$$src.cpp'" | grep "Lines executed" | head -1); \
	  printf "  %-16s %s\n" "$$src.cpp" "$$line"; \
	done
	@echo ""
	@echo "Raw gcov data in .tmp/cov/. For HTML: lcov --capture on that dir."

## ---- Mutation testing (mull) ----------------------------------------------

mutate: ## Run mutation testing with mull (needs mull + clang IR plugin)
	@command -v mull-runner-18 >/dev/null 2>&1 || command -v mull-runner >/dev/null 2>&1 || \
	  { echo "mull-runner not found. Install mull (https://mull.readthedocs.io) then retry."; exit 1; }
	@MULL_PLUGIN=$$(ls /usr/lib/mull-ir-frontend* /opt/homebrew/lib/mull-ir-frontend* 2>/dev/null | head -1); \
	 CLANG=$$(command -v clang++-18 || command -v clang++); \
	 if [ -z "$$MULL_PLUGIN" ]; then echo "mull IR frontend plugin not found"; exit 1; fi; \
	 mkdir -p ../../.tmp/mull; \
	 $$CLANG $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -O0 -g -grecord-command-line \
	   -DKEUR_TEST_ROOT='"$(REPO_ROOT)"' -fpass-plugin=$$MULL_PLUGIN \
	   -o ../../.tmp/mull/keur-test-mull $(COV_SRCS) $(RE2_LDFLAGS) && \
	 MULL_RUNNER=$$(command -v mull-runner-18 || command -v mull-runner); \
	 $$MULL_RUNNER ../../.tmp/mull/keur-test-mull --test-program-arg=--test-suite-exclude=perf

clean: ## Remove build artifacts
	rm -f $(BIN) $(TEST_BIN) $(MAIN_OBJ) $(TEST_OBJ) $(IMPL_OBJS) $(RULES_OBJ)
	rm -rf ../../.tmp/cov ../../.tmp/mull
