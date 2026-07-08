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

.PHONY: all clean debug release native test

all: release

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

# Quick test: perft from start position
test: release
	@printf "position startpos\nperft 6\nquit\n" | ./$(TARGET)
