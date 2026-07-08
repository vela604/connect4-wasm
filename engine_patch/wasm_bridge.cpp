// wasm_bridge.cpp
//
// Thin entry point used ONLY for the WebAssembly build. The native CLI
// build (main.cpp) keeps its interactive stdin REPL via UCI::run();
// this file replaces that with two tiny functions the JS Web Worker
// calls directly through Module.ccall()/cwrap(), because a blocking
// std::getline(std::cin, ...) loop has nothing to read from inside a
// browser and can't be driven by postMessage() events.
//
// engine_init()        — construct the single UCI instance in web_mode.
// send_uci_command(s)  — dispatch one UCI-style command line, e.g.
//                         "position startpos moves 3 4 2", "go infinite",
//                         "stop". Returns immediately; results/progress
//                         stream back asynchronously via stdout, which
//                         Emscripten routes to Module.print() — captured
//                         by engine-worker.js and parsed there.
//
// Build (see the `web` target added to the Makefile):
//   emcc -std=c++17 -O3 -DNDEBUG -pthread \
//        -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=8 \
//        -s EXPORTED_FUNCTIONS='["_engine_init","_send_uci_command","_main"]' \
//        -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
//        -s MODULARIZE=1 -s EXPORT_NAME='Connect4Module' \
//        -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=worker \
//        engine.cpp uci.cpp wasm_bridge.cpp -o web/connect4.js
//
// NOTE on threads: this engine's Lazy-SMP search and the UCI search
// thread both use std::thread, which Emscripten maps onto real Web
// Workers backed by SharedArrayBuffer. That requires the page serving
// engine-worker.js to be cross-origin isolated:
//   Cross-Origin-Opener-Policy: same-origin
//   Cross-Origin-Embedder-Policy: require-corp
// Most static file servers don't set these by default — see README.md.

#include "uci.h"
#include <emscripten.h>
#include <memory>

namespace {
    std::unique_ptr<UCI> g_uci;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void engine_init() {
    if (!g_uci) g_uci = std::make_unique<UCI>(/*web_mode=*/true);
}

EMSCRIPTEN_KEEPALIVE
void send_uci_command(const char* cmd) {
    if (!g_uci) engine_init();
    g_uci->execute(cmd ? std::string(cmd) : std::string());
}

} // extern "C"

// Emscripten still wants a main() to link; the worker never calls it,
// it only calls the exported functions above.
int main() {
    return 0;
}
