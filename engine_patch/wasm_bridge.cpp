// wasm_bridge.cpp
//
// Thin entry point used ONLY for the WebAssembly build. The native CLI
// build (main.cpp) keeps its interactive stdin REPL via UCI::run();
// this file replaces that with three tiny functions the JS Web Worker
// calls directly through Module.ccall()/cwrap(), because a blocking
// std::getline(std::cin, ...) loop has nothing to read from inside a
// browser and can't be driven by postMessage() events.
//
// engine_init()        — construct the single UCI instance in web_mode.
// send_uci_command(s)  — dispatch one UCI-style command line, e.g.
//                         "position startpos moves 3 4 2", "go infinite",
//                         "stop". For "go", this only STARTS the search
//                         (see step_search() below) — it never blocks.
// step_search()         — advance the current "go infinite" search by
//                         exactly one depth and return whether there's
//                         more to do (1) or it's finished (0).
//
// Why step_search() instead of a background thread: the original
// design (still used by the native CLI build) runs the search on a
// std::thread so "stop" can interrupt it while it keeps working. Real
// OS threads under Emscripten need SharedArrayBuffer, which browsers
// only allow on pages served with:
//   Cross-Origin-Opener-Policy: same-origin
//   Cross-Origin-Embedder-Policy: require-corp
// which many static hosts don't set by default (this is exactly what
// caused the engine to hang silently instead of ever calling back).
// Engine::step_once() (see engine.h/engine.cpp) already only needs to
// run ONE iterative-deepening depth per call and returns control right
// back — so JS can drive "infinite" search itself with a plain
// setTimeout(..., 0) loop, from a single-threaded Wasm build that
// needs no special headers on ANY host at all. See engine-worker.js.
//
// Build (see the `web` target in the Makefile — no -pthread, no
// USE_PTHREADS, so no cross-origin-isolation requirement whatsoever):
//   emcc -std=c++17 -O3 -DNDEBUG \
//        -s EXPORTED_FUNCTIONS='["_engine_init","_send_uci_command","_step_search","_main"]' \
//        -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
//        -s MODULARIZE=1 -s EXPORT_NAME='Connect4Module' \
//        -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=worker \
//        engine.cpp uci.cpp wasm_bridge.cpp -o web/connect4.js

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

EMSCRIPTEN_KEEPALIVE
int step_search() {
    if (!g_uci) return 0;
    return g_uci->step() ? 1 : 0;
}

} // extern "C"

// Emscripten still wants a main() to link; the worker never calls it,
// it only calls the exported functions above.
int main() {
    return 0;
}

