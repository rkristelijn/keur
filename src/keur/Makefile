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
# -MMD -MP → emit per-object .d files so header edits trigger the right rebuilds
#            automatically (ADR-008). Belt-and-braces with the explicit header
#            prerequisites below; the .d files also catch newly-added includes
#            that a hand-written prerequisite list would miss.
CXXFLAGS ?= -Wall -Wextra -std=c++20 -O2 -MMD -MP -DKEUR_VERSION='"$(VERSION)"'
INCLUDES  = -I. -I.. -I../..

# Platform abstraction source selection — @see docs/adr/ADR-005.
# The correct translation unit is compiled per OS; business code holds no
# #ifdef. On Windows (nmake/mingw make) $(OS) is "Windows_NT".
ifeq ($(OS),Windows_NT)
  PLATFORM_SRC = common/platform_win32.cpp
else
  PLATFORM_SRC = common/platform_posix.cpp
endif
PLATFORM_OBJ = $(BINDIR)/keur_platform.o

# RE2 + abseil
RE2_PREFIX  ?= $(shell brew --prefix re2  2>/dev/null || echo /usr)
ABSL_PREFIX ?= $(shell brew --prefix abseil 2>/dev/null || echo /usr)
RE2_CFLAGS   = -I$(RE2_PREFIX)/include -I$(ABSL_PREFIX)/include
RE2_LDFLAGS  = -L$(RE2_PREFIX)/lib -L$(ABSL_PREFIX)/lib -lre2

BINDIR = ../../bin
BIN    = $(BINDIR)/keur

REPO_ROOT := $(shell cd ../.. && pwd -P)

# UTF-8 locale for codegen (gen-catalog bakes rule titles into C++ strings; a
# non-UTF-8 locale like CI's LC_ALL=C splits multibyte chars — e.g. em-dashes —
# mid-byte and emits broken string literals). Pick one the host actually has:
# C.UTF-8 on Linux/CI, en_US.UTF-8 on macOS.
UTF8_LOCALE := $(shell locale -a 2>/dev/null | grep -iqE '^C\.utf-?8$$' && echo C.UTF-8 || echo en_US.UTF-8)

# Rule engine (shared, lower layer)
RULES_OBJ = $(BINDIR)/rule_parse.o $(BINDIR)/rule_match.o $(BINDIR)/rule_scan.o $(BINDIR)/rule_block.o $(BINDIR)/rule_engines.o $(BINDIR)/rule_facts.o $(BINDIR)/rule_semver.o $(BINDIR)/rule_composite.o $(BINDIR)/rule_resolve.o
TOKENIZER_OBJ = $(BINDIR)/tokenizer.o

# All keur sources (relative to src/keur)
MAIN_SRC   = main.cpp
IMPL_SRCS  = cmd/check.cpp cmd/hook.cpp cmd/scan.cpp cmd/policy.cpp cmd/train.cpp cmd/learn.cpp cmd/rules.cpp cmd/config.cpp cmd/doctor.cpp cmd/manager.cpp cmd/level.cpp cmd/difficulty.cpp cmd/settings.cpp cmd/tune.cpp \
             config/scopes.cpp config/hookmgr.cpp config/scanners.cpp config/disabled.cpp config/keurignore.cpp config/ruleset.cpp \
             scan/dispatch.cpp scan/mappers.cpp scan/json_util.cpp \
             learn/history.cpp learn/sysinfo.cpp learn/monitor.cpp \
             adapters/git.cpp adapters/fs.cpp adapters/proc.cpp adapters/sysinfo.cpp report/report.cpp \
             render/display_width.cpp render/box.cpp render/logo.cpp render/logo_generated.cpp common/keyinput.cpp \
             manager/model.cpp manager/view.cpp manager/catalog_generated.cpp
# Object files: flatten path (cmd/check.cpp -> keur_cmd_check.o) via subst
IMPL_OBJS  = $(patsubst %.cpp,$(BINDIR)/keur_%.o,$(subst /,_,$(IMPL_SRCS)))
MAIN_OBJ   = $(BINDIR)/keur_main.o
TEST_OBJ   = $(BINDIR)/keur_test.o
TEST_BIN   = $(BINDIR)/keur-test

.PHONY: all build test test-unit test-integration perf e2e coverage mutate compile-commands lint clean help
.DEFAULT_GOAL := build

# Auto-generated header dependency files (-MMD). Pull them in so a changed header
# rebuilds exactly the objects that include it — no manual prerequisite upkeep.
# The '-' prefix ignores them on the first (clean) build when none exist yet.
DEPS = $(MAIN_OBJ:.o=.d) $(IMPL_OBJS:.o=.d) $(TEST_OBJ:.o=.d) \
       $(RULES_OBJ:.o=.d) $(TOKENIZER_OBJ:.o=.d) $(PLATFORM_OBJ:.o=.d)
-include $(DEPS)

help: ## Show targets
	@grep -E '^[a-zA-Z_-]+:.*## ' $(MAKEFILE_LIST) | sort | \
	  awk 'BEGIN{FS=":.*## "}{printf "  \033[36m%-18s\033[0m %s\n", $$1, $$2}'

## ---- Build ----------------------------------------------------------------

build: $(BIN) ## Build bin/keur

$(BINDIR):
	@mkdir -p $(BINDIR)

$(BINDIR)/rule_parse.o: ../rules/rule/parse.cpp ../rules/rule/internal.h ../rules/rule_engine.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/rule_match.o: ../rules/rule/match.cpp ../rules/rule/internal.h ../rules/rule_engine.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/rule_scan.o: ../rules/rule/scan.cpp ../rules/rule/internal.h ../rules/rule_engine.h ../rules/tokenizer.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/rule_block.o: ../rules/rule/block.cpp ../rules/rule/internal.h ../rules/rule_engine.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/rule_engines.o: ../rules/rule/engines.cpp ../rules/rule/internal.h ../rules/rule_engine.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/rule_facts.o: ../rules/rule/facts.cpp ../rules/rule/facts.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/rule_semver.o: ../rules/rule/semver.cpp ../rules/rule/semver.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/rule_composite.o: ../rules/rule/composite.cpp ../rules/rule/internal.h ../rules/rule_engine.h ../rules/rule/semver.h ../rules/rule/facts.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(BINDIR)/rule_resolve.o: ../rules/rule/resolve.cpp ../rules/rule/internal.h ../rules/rule_engine.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

$(TOKENIZER_OBJ): ../rules/tokenizer.cpp ../rules/tokenizer.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -c $< -o $@

# Object compilation (ADR-008: incremental compile).
#
# One source → one object. We DON'T hand-list each object's header
# prerequisites: `-MMD -MP` emits a `.d` per object with its real includes, and
# `-include $(DEPS)` (above) pulls them in, so a changed header rebuilds exactly
# the objects that use it — automatically and completely. Hand-written
# prerequisite lists were redundant with that and went stale (a new source
# needed a second edit or the build broke). So a new source is now ONE line in
# IMPL_SRCS and nothing else.
#
# We generate one compile rule per source with `foreach`/`eval`, keyed on the
# REAL source path (so object names containing underscores, e.g. scan/json_util
# → keur_scan_json_util.o, map correctly — a blind `_`→`/` substitution would
# not). Sources that pull in the rule engine get -I../rules; the rest don't.
RULES_DEP_SRCS = cmd/learn.cpp cmd/difficulty.cpp cmd/settings.cpp cmd/tune.cpp config/ruleset.cpp report/report.cpp

# src → flattened object path (cmd/doctor.cpp → $(BINDIR)/keur_cmd_doctor.o)
obj_of = $(BINDIR)/keur_$(subst /,_,$(1:.cpp=.o))

# $(call emit_rule,<src>,<extra-flags>)
define emit_rule
$(call obj_of,$(1)): $(1) | $$(BINDIR)
	$$(CXX) $$(CXXFLAGS) $$(INCLUDES) $(2) $$(RE2_CFLAGS) -c $$< -o $$@
endef

# main.o — its own rule (not flattened).
$(MAIN_OBJ): main.cpp | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

# Rule-engine-dependent sources: with -I../rules.
$(foreach s,$(RULES_DEP_SRCS),$(eval $(call emit_rule,$(s),-I../rules)))
# Everything else in IMPL_SRCS: standard flags.
$(foreach s,$(filter-out $(RULES_DEP_SRCS),$(IMPL_SRCS)),$(eval $(call emit_rule,$(s),)))

# Platform abstraction: compile the OS-selected translation unit ($(PLATFORM_SRC))
# into one object. Only this object contains platform #ifdefs / OS headers.
$(PLATFORM_OBJ): $(PLATFORM_SRC) common/platform.h common/compat.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) -c $< -o $@

# Shared, pure key decoder, and the manager TUI model/view, are compiled by the
# generic foreach rules above (they are in IMPL_SRCS). Only generation steps and
# the platform-selected translation unit need their own rules below.

# Compile-time rule catalogue: generated from the .rule files by
# scripts/gen-catalog.sh, then baked into the binary. Regenerated whenever any
# .rule under rules/ changes. @implements KEUR-MANAGER-CATALOG
RULE_FILES := $(shell find $(REPO_ROOT)/rules -name '*.rule' 2>/dev/null)
manager/catalog_generated.cpp: $(REPO_ROOT)/scripts/gen-catalog.sh $(RULE_FILES)
	LC_ALL=$(UTF8_LOCALE) $(REPO_ROOT)/scripts/gen-catalog.sh $@ $(REPO_ROOT)/rules

.PHONY: catalog
catalog: manager/catalog_generated.cpp ## Regenerate the baked rule catalogue
# (catalog_generated.o itself is compiled by the generic foreach rule.)

# Compile-time logo assets: baked from the logo PNG by scripts/gen-logo.sh into
# render/logo_generated.cpp (committed, like the rule catalogue). Regenerated
# on demand via `make logo`; the build does NOT run ffmpeg/node (ADR-002).
# @implements KEUR-LOGO
LOGO_PNG    ?= $(REPO_ROOT)/docs/design/assets/keur-logo-source.png
LOGO_CONV   ?= $(REPO_ROOT)/scripts/img2braille.js
render/logo_generated.cpp: $(REPO_ROOT)/scripts/gen-logo.sh $(LOGO_PNG) $(LOGO_CONV)
	$(REPO_ROOT)/scripts/gen-logo.sh $@ $(LOGO_PNG) $(LOGO_CONV)

.PHONY: logo
logo: render/logo_generated.cpp ## Regenerate the baked braille logo (needs ffmpeg+node)
# (logo_generated.o and report.o are compiled by the generic foreach rules.)

$(BIN): $(MAIN_OBJ) $(IMPL_OBJS) $(RULES_OBJ) $(TOKENIZER_OBJ) $(PLATFORM_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(RE2_LDFLAGS)
	@echo "built $(BIN) (version $(VERSION))"

## ---- Test binary ----------------------------------------------------------

$(TEST_OBJ): keur_test.cpp tests/test_helpers.h cmd/check.h cmd/hook.h cmd/scan.h cmd/policy.h cmd/config.h cmd/manager.h cmd/level.h render/logo.h config/scopes.h config/hookmgr.h config/scanners.h config/disabled.h config/ruleset.h \
             adapters/git.h adapters/fs.h adapters/proc.h adapters/sysinfo.h learn/sysinfo.h report/report.h util/keur_utils.h \
             ../rules/tokenizer.h ../../vendor/doctest.h | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../rules $(RE2_CFLAGS) -DKEUR_TEST_ROOT='"$(REPO_ROOT)"' -c $< -o $@

$(TEST_BIN): $(TEST_OBJ) $(IMPL_OBJS) $(RULES_OBJ) $(TOKENIZER_OBJ) $(PLATFORM_OBJ)
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
           adapters/git.cpp adapters/fs.cpp adapters/proc.cpp report/report.cpp $(PLATFORM_SRC) ../rules/rule/parse.cpp ../rules/rule/match.cpp ../rules/rule/scan.cpp ../rules/rule/block.cpp ../rules/rule/engines.cpp ../rules/rule/resolve.cpp ../rules/tokenizer.cpp

coverage: ## Build test binary with coverage and report (gcov)
	@mkdir -p ../../.tmp/cov
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RE2_CFLAGS) --coverage -O0 \
	  -DKEUR_TEST_ROOT='"$(REPO_ROOT)"' \
	  -o ../../.tmp/cov/keur-test-cov $(COV_SRCS) $(RE2_LDFLAGS)
	cd ../../.tmp/cov && ./keur-test-cov --test-suite-exclude=perf >/dev/null 2>&1 || true
	@echo ""
	@echo "Coverage (keur sources):"
	@cd ../../.tmp/cov && for src in check hook scan policy git fs proc report; do \
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

## ---- Compilation database (for clang tooling / IDEs / cpm) ----------------

# Generate ../../compile_commands.json describing how each source is compiled,
# including our -I include paths. Any clang-based tool (clang-tidy, clangd, cpm,
# an IDE) reads this and needs no per-tool include config. No extra dependency:
# we know our own sources and flags, so we emit the JSON directly.
# Works identically on macOS and Linux.
compile-commands: ## Write compile_commands.json at the repo root
	@echo "generating $(REPO_ROOT)/compile_commands.json"
	@dir="$(CURDIR)"; \
	 flags="-std=c++20 $(INCLUDES) $(RE2_CFLAGS)"; \
	 { echo "["; \
	   first=1; \
	   for src in main.cpp keur_test.cpp $(IMPL_SRCS); do \
	     [ $$first -eq 1 ] || echo "  ,"; first=0; \
	     printf '  {\n'; \
	     printf '    "directory": "%s",\n' "$$dir"; \
	     printf '    "command": "c++ %s -c %s",\n' "$$flags" "$$src"; \
	     printf '    "file": "%s/%s"\n' "$$dir" "$$src"; \
	     printf '  }\n'; \
	   done; \
	   echo "]"; \
	 } > "$(REPO_ROOT)/compile_commands.json"
	@echo "wrote $$(grep -c '"file"' "$(REPO_ROOT)/compile_commands.json") entries"

## ---- Lint (clang-tidy if available; else compiler warnings) ---------------

lint: ## Run clang-tidy if installed; otherwise a -Wall -Wextra syntax check
	@if command -v clang-tidy >/dev/null 2>&1; then \
	   echo "clang-tidy:"; \
	   clang-tidy --quiet $(IMPL_SRCS) main.cpp -- -std=c++20 $(INCLUDES) $(RE2_CFLAGS); \
	 else \
	   echo "clang-tidy not found; falling back to compiler warnings (-Wall -Wextra)"; \
	   for src in $(IMPL_SRCS) main.cpp; do \
	     c++ -std=c++20 -Wall -Wextra $(INCLUDES) $(RE2_CFLAGS) -fsyntax-only "$$src" || exit 1; \
	   done; \
	   echo "syntax + warning check clean"; \
	 fi
	@echo ""
	@echo "basics (file/dir size norms — KEUR-QUAL-005/006, via the engine):"
	@if [ -x "$(BIN)" ]; then \
	   hits=$$(KEUR_RULES_DIRS="$(REPO_ROOT)/rules/quality" "$(BIN)" scan "$(REPO_ROOT)/src" 2>/dev/null \
	           | grep -cE '"rule":"KEUR-QUAL-00[56]"' || true); \
	   if [ "$$hits" -gt 0 ]; then \
	     KEUR_RULES_DIRS="$(REPO_ROOT)/rules/quality" "$(BIN)" scan "$(REPO_ROOT)/src" 2>/dev/null \
	       | grep -E '"rule":"KEUR-QUAL-00[56]"'; \
	     echo "basics: $$hits violation(s) (file>300 net lines / dir>10 units)"; \
	   else \
	     echo "basics: OK"; \
	   fi; \
	 else \
	   echo "basics: keur not built yet (run 'make build') — skipping"; \
	 fi

clean: ## Remove build artifacts
	rm -f $(BIN) $(TEST_BIN) $(MAIN_OBJ) $(TEST_OBJ) $(IMPL_OBJS) $(RULES_OBJ) $(TOKENIZER_OBJ) $(PLATFORM_OBJ)
	rm -f $(DEPS)
	rm -rf ../../.tmp/cov ../../.tmp/mull
