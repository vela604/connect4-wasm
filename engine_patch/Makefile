CXX      := g++
# NOTE on portability:
#   -march=native was removed from the default build. It tunes the
#   binary for whatever CPU does the COMPILING, which is unsafe to
#   ship/copy to a different machine (can crash with "illegal
#   instruction" on an older/different CPU) and is also not
#   understood by MSVC on Windows. Use `make native` below if you
#   specifically want a binary optimized for (and only safely
#   runnable on) the exact machine you're building on.
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -pthread
TARGET   := connect4
SRCS     := main.cpp engine.cpp uci.cpp
HEADERS  := bitboard.h tt.h evaluator.h engine.h uci.h

.PHONY: all clean debug release native test web

all: release

# WebAssembly build for the browser analysis board (see engine_patch/README.md
# and wasm_bridge.cpp for details). Requires the Emscripten SDK (emcc) on PATH:
#   https://emscripten.org/docs/getting_started/downloads.html
#
# Single-threaded on purpose: the "go infinite" search is driven one
# depth at a time from JS via step_search() (see wasm_bridge.cpp /
# engine.h's start_stepped_search()/step_once()), so no std::thread /
# SharedArrayBuffer is needed at all — this runs on ANY static host,
# no Cross-Origin-Opener-Policy/Cross-Origin-Embedder-Policy headers
# required.
EMCC       := emcc
WEB_SRCS   := engine.cpp uci.cpp wasm_bridge.cpp
WEB_OUTDIR := web
EMFLAGS    := -std=c++17 -O3 -DNDEBUG \
              -s EXPORTED_FUNCTIONS='["_engine_init","_send_uci_command","_step_search","_main"]' \
              -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
              -s MODULARIZE=1 -s EXPORT_NAME='Connect4Module' \
              -s ALLOW_MEMORY_GROWTH=1 \
              -s ENVIRONMENT=worker

web: $(WEB_SRCS) $(HEADERS)
	mkdir -p $(WEB_OUTDIR)
	$(EMCC) $(EMFLAGS) $(WEB_SRCS) -o $(WEB_OUTDIR)/connect4.js
	@echo "  Built: $(WEB_OUTDIR)/connect4.js + $(WEB_OUTDIR)/connect4.wasm"
	@echo "  Copy both into the web app's directory (next to engine-worker.js)."
	@echo "  Single-threaded build — works on any static host, no special headers needed."

release: $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -DNDEBUG $(SRCS) -o ./$(TARGET)
	@echo "  Built: ./$(TARGET)  (portable -O3 build; auto-detects CPU cores at runtime)"

# Optional: max performance for THIS exact machine only (don't copy
# this binary elsewhere — see note above).
native: $(SRCS) $(HEADERS)
	$(CXX) -std=c++17 -O3 -march=native -Wall -Wextra -pthread \
	       -DNDEBUG $(SRCS) -o ./$(TARGET)_native
	@echo "  Built: ./$(TARGET)_native  (tuned for this CPU — do not redistribute)"

debug: $(SRCS) $(HEADERS)
	$(CXX) -std=c++17 -g -O0 -Wall -Wextra -pthread \
	       $(SRCS) -o ./$(TARGET)_debug
	@echo "  Built: ./$(TARGET)_debug"

clean:
	rm -f ./$(TARGET) ./$(TARGET)_native ./$(TARGET)_debug *.o
	rm -rf $(WEB_OUTDIR)

# Quick test: perft from start position
test: release
	@printf "position startpos\nperft 6\nquit\n" | ./$(TARGET)
