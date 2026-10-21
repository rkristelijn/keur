# Makefile — keur-rules native engine (RE2-powered, follows cpm's build).
# Output: bin/keur-rules
CXX      ?= c++
CXXFLAGS ?= -Wall -Wextra -std=c++20 -O2

# RE2 + abseil (Homebrew on macOS, system paths on Linux).
RE2_PREFIX  ?= $(shell brew --prefix re2 2>/dev/null || echo /usr)
ABSL_PREFIX ?= $(shell brew --prefix abseil 2>/dev/null || echo /usr)
RE2_CFLAGS  = -I$(RE2_PREFIX)/include -I$(ABSL_PREFIX)/include
RE2_LDFLAGS = -L$(RE2_PREFIX)/lib -L$(ABSL_PREFIX)/lib -lre2

# Shared platform seam (ADR-005). The engine reaches DOWN to the common
# platform layer for exactly one thing: a shell-free `git ls-files` capture
# (gitignore-aware file-presence rules — ADR-166). It is the SAME abstraction
# src/keur links, not an app dependency: `-I..` resolves keur/common/platform.h
# from src/, and the OS-selected translation unit is compiled in here too, so
# keur-rules stays standalone-buildable. Business code holds no #ifdef.
ifeq ($(OS),Windows_NT)
  PLATFORM_SRC = ../keur/common/platform_win32.cpp ../keur/common/keyinput.cpp
else
  PLATFORM_SRC = ../keur/common/platform_posix.cpp ../keur/common/keyinput.cpp
endif

BINDIR = ../../bin
BIN    = $(BINDIR)/keur-rules
SRC    = rule/parse.cpp rule/match.cpp rule/scan.cpp rule/block.cpp rule/engines.cpp rule/facts.cpp rule/semver.cpp rule/composite.cpp rule/resolve.cpp rule/pipeline.cpp tokenizer.cpp main.cpp $(PLATFORM_SRC)

.PHONY: all clean
all: $(BIN)

$(BIN): $(SRC) rule_engine.h rule/internal.h rule/facts.h rule/semver.h tokenizer.h
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -I. -I.. -I../keur $(RE2_CFLAGS) $(SRC) -o $(BIN) $(RE2_LDFLAGS)
	@echo "built $(BIN)"

clean:
	rm -f $(BIN)
