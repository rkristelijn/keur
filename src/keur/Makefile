# Makefile — keur single binary + full test pipeline.
#
# Build:            make               (build bin/keur)
# Test (all):       make test          (unit + integration + e2e)
# Test tiers:       make test-unit  make test-integration  make e2e  make perf
# Coverage:         make coverage      (llvm-cov / gcov report)
# Mutation testing: make mutate        (requires mull-runner + clang plugin)
# Clean:            make clean
#
# Dependencies: RE2 + abseil (Homebrew on macOS, system packages on Linux).
# The rule engine (src/rules/) compiles to an object and links into every target.

CXX      ?= c++
VERSION  := $(shell grep '^version' ../../config/keur.toml 2>/dev/null | head -1 | sed 's/.*=[ ]*//; s/"//g; s/[[:space:]]//g')
VERSION  := $(or $(VERSION),dev)

CXXFLAGS ?= -Wall -Wextra -std=c++20 -O2 -DKEUR_VERSION='"$(VERSION)"'
INCLUDES  = -I../.. -I..

# RE2 + abseil
RE2_PREFIX  ?= $(shell brew --prefix re2  2>/dev/null || echo /usr)
ABSL_PREFIX ?= $(shell brew --prefix abseil 2>/dev/null || echo /usr)
RE2_CFLAGS   = -I$(RE2_PREFIX)/include -I$(ABSL_PREFIX)/include
RE2_LDFLAGS  = -L$(RE2_PREFIX)/lib -L$(ABSL_PREFIX)/lib -lre2

BINDIR = ../../bin
BIN    = $(BINDIR)/keur

# Rule engine (shared)
RULES_SRC = ../rules/rule_engine.cpp
RULES_OBJ = $(BINDIR)/rule_engine.o

# keur binary object files
KEUR_SRCS = main.cpp cmd_check.cpp cmd_hook.cpp cmd_scan.cpp cmd_policy.cpp
KEUR_OBJS = $(patsubst %.cpp,$(BINDIR)/keur_%.o,$(KEUR_SRCS))

# The subcommand implementations, minus main.cpp — shared by binary + tests.
IMPL_OBJS = $(BINDIR)/keur_cmd_check.o $(BINDIR)/keur_cmd_hook.o \
            $(BINDIR)/keur_cmd_scan.o $(BINDIR)/keur_cmd_policy.o

TEST_BIN  = $(BINDIR)/keur-test

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

$(BINDIR)/keur_main.o: main.cpp cmd_check.h cmd_hook.h cmd_scan.h cmd_policy.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_cmd_check.o: cmd_check.cpp cmd_check.h cmd_policy.h keur_utils.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_cmd_hook.o: cmd_hook.cpp cmd_hook.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_cmd_scan.o: cmd_scan.cpp cmd_scan.h keur_utils.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/keur_cmd_policy.o: cmd_policy.cpp cmd_policy.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

$(BIN): $(KEUR_OBJS) $(RULES_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(RE2_LDFLAGS)
	@echo "built $(BIN) (version $(VERSION))"

## ---- Test binary ----------------------------------------------------------

# Repo root resolved at configure time — passed to tests so they can locate
# tests/fixtures/ and rules/ regardless of the working directory.
REPO_ROOT := $(shell cd ../.. && pwd -P)

$(BINDIR)/keur_test.o: keur_test.cpp cmd_check.h cmd_hook.h cmd_scan.h cmd_policy.h keur_utils.h ../../vendor/doctest.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -DKEUR_TEST_ROOT='"$(REPO_ROOT)"' -c $< -o $@

$(TEST_BIN): $(BINDIR)/keur_test.o $(IMPL_OBJS) $(RULES_OBJ)
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

coverage: ## Build test binary with coverage and report (gcov)
	@mkdir -p ../../.tmp/cov
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) --coverage -O0 \
	  -DKEUR_TEST_ROOT='"$(REPO_ROOT)"' \
	  -o ../../.tmp/cov/keur-test-cov \
	  keur_test.cpp cmd_check.cpp cmd_hook.cpp cmd_scan.cpp cmd_policy.cpp \
	  ../rules/rule_engine.cpp $(RE2_LDFLAGS)
	cd ../../.tmp/cov && ./keur-test-cov --test-suite-exclude=perf >/dev/null 2>&1 || true
	@echo ""
	@echo "Coverage (keur subcommands):"
	@cd ../../.tmp/cov && for src in cmd_check cmd_hook cmd_scan cmd_policy; do \
	  line=$$(gcov "keur-test-cov-$$src" 2>/dev/null | grep -A1 "File '$$src.cpp'" | grep "Lines executed" | head -1); \
	  printf "  %-16s %s\n" "$$src.cpp" "$$line"; \
	done
	@echo ""
	@echo "Raw gcov data in .tmp/cov/. For HTML: lcov --capture on that dir."

## ---- Mutation testing (mull) ----------------------------------------------

mutate: ## Run mutation testing with mull (needs mull + clang IR plugin)
	@command -v mull-runner-18 >/dev/null 2>&1 || command -v mull-runner >/dev/null 2>&1 || \
	  { echo "mull-runner not found. Install mull (https://mull.readthedocs.io) then retry."; exit 1; }
	@echo "Building test binary with mull IR plugin..."
	@MULL_PLUGIN=$$(ls /usr/lib/mull-ir-frontend* /opt/homebrew/lib/mull-ir-frontend* 2>/dev/null | head -1); \
	 CLANG=$$(command -v clang++-18 || command -v clang++); \
	 if [ -z "$$MULL_PLUGIN" ]; then echo "mull IR frontend plugin not found"; exit 1; fi; \
	 mkdir -p ../../.tmp/mull; \
	 $$CLANG $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -O0 -g -grecord-command-line \
	   -DKEUR_TEST_ROOT='"$(REPO_ROOT)"' \
	   -fpass-plugin=$$MULL_PLUGIN \
	   -o ../../.tmp/mull/keur-test-mull \
	   keur_test.cpp cmd_check.cpp cmd_hook.cpp cmd_scan.cpp cmd_policy.cpp \
	   ../rules/rule_engine.cpp $(RE2_LDFLAGS) && \
	 MULL_RUNNER=$$(command -v mull-runner-18 || command -v mull-runner); \
	 $$MULL_RUNNER ../../.tmp/mull/keur-test-mull --test-program-arg=--test-suite-exclude=perf

clean: ## Remove build artifacts
	rm -f $(BIN) $(TEST_BIN) $(KEUR_OBJS) $(BINDIR)/keur_main.o \
	      $(BINDIR)/keur_test.o $(RULES_OBJ)
	rm -rf ../../.tmp/cov ../../.tmp/mull
