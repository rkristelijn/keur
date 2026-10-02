# Makefile — keur-rules native engine (RE2-powered, follows cpm's build).
# Output: bin/keur-rules
CXX      ?= c++
CXXFLAGS ?= -Wall -Wextra -std=c++20 -O2

# RE2 + abseil (Homebrew on macOS, system paths on Linux).
RE2_PREFIX  ?= $(shell brew --prefix re2 2>/dev/null || echo /usr)
ABSL_PREFIX ?= $(shell brew --prefix abseil 2>/dev/null || echo /usr)
RE2_CFLAGS  = -I$(RE2_PREFIX)/include -I$(ABSL_PREFIX)/include
RE2_LDFLAGS = -L$(RE2_PREFIX)/lib -L$(ABSL_PREFIX)/lib -lre2

BINDIR = ../../bin
BIN    = $(BINDIR)/keur-rules
SRC    = rule/parse.cpp rule/match.cpp rule/scan.cpp rule/block.cpp rule/engines.cpp rule/facts.cpp rule/semver.cpp rule/composite.cpp rule/resolve.cpp tokenizer.cpp main.cpp

.PHONY: all clean
all: $(BIN)

$(BIN): $(SRC) rule_engine.h rule/internal.h rule/facts.h rule/semver.h tokenizer.h
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -I. $(RE2_CFLAGS) $(SRC) -o $(BIN) $(RE2_LDFLAGS)
	@echo "built $(BIN)"

clean:
	rm -f $(BIN)
