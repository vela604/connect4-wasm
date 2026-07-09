# Engine patch: WebAssembly bridge (single-threaded)

I read through your actual source (`bitboard.h`, `engine.h`/`.cpp`,
`uci.h`/`.cpp`, `tt.h`, `evaluator.h`, `main.cpp`) before writing the
front end. Three things there needed real changes to be callable from
a browser at all — not just "shape the JS around what exists":

## 1. No JS entry point existed

`main.cpp` / `UCI::run()` is an interactive REPL blocking on
`std::getline(std::cin, ...)`. There's no stdin in a browser, and a
blocking read loop can't be driven by `postMessage` events anyway.

**Fix:** extracted the per-line command dispatch out of `run()`'s
while-loop into a new public `UCI::execute(const std::string&)`, and
added `wasm_bridge.cpp`, exporting `engine_init()`/`send_uci_command()`
via `EMSCRIPTEN_KEEPALIVE`/`extern "C"`.

## 2. Output wasn't machine-parseable

`print_info()`/`print_final_result()` print human-oriented ANSI-colored
text (`format_score()`, box-drawing separators, etc.) — great for a
terminal, not something you want to regex in JS.

**Fix:** added a `web_mode` flag to `UCI` (`UCI(bool web_mode = false)`).
When true, those functions print one clean, colorless line per event
instead, built from the real fields on `SearchResult`/`SearchStats`:

```
info depth 8 score cp 42 nodes 14205 nps 450000 time 31 ttpct 12.4 tthits 900 pv 3 4 2
info depth 12 score mate 4 nodes 88210 nps 512000 time 172 ttpct 30.1 tthits 5000 pv 3 2 1
bestmove 3
```

Notes baked into the front end because of this:
- `score` is this engine's own raw evaluation unit, **not centipawns**
  (`SCORE_WIN = 1,000,000`, `MATE_THRESHOLD = 900,000`, from
  `bitboard.h`). The JS treats it as a ~±1,500-range raw score and
  scales it with `tanh` for the eval bar.
- `mate_in` is already signed (`>0` = win in N plies, `<0` = lose in
  N), matching `engine.h` — passed straight through.
- `nps` isn't tracked by `SearchStats` at all, so a `go_start_`
  timestamp was added, and `print_info` computes
  `nodes * 1000 / elapsed_ms`.

## 3. "go infinite" required a real OS thread — removed entirely

The original design (still used by the native CLi build) runs
`engine_.search()` on a `std::thread` so `stop` can interrupt it later.
Emscripten maps real threads onto Web Workers backed by
`SharedArrayBuffer`, which browsers only allow on pages served with
`Cross-Origin-Opener-Policy`/`Cross-Origin-Embedder-Policy` headers —
and critically, when those headers are missing, the module doesn't
throw an error, it just **hangs silently forever**. That's exactly
what happened during setup: `connect4.js` loaded fine, but the module
never finished initializing and no error ever surfaced.

**Fix:** `Engine::search()` already had a single-threaded code path
(`iterative_deepen()` in a plain loop, no thread) for low-end
devices/explicit overrides — I built on that rather than replacing it.
Added to `engine.h`/`engine.cpp`:

```cpp
void start_stepped_search(const Position& pos, int fixed_depth = 0);
bool step_once();                 // runs exactly one more depth, returns false when done
bool stepped_search_active() const;
const SearchResult& stepped_result() const;
```

`step_once()` is exactly one iteration of the same iterative-deepening
loop `search()` already ran internally — same `negamax()` call, same
`progress_cb_` firing after each depth — just returning control to the
caller after each depth instead of looping internally. `UCI::step()`
wraps this for the bridge; `wasm_bridge.cpp` exports it as
`step_search()`. **`engine-worker.js` drives "infinite" search itself**
with a plain `setTimeout(..., 0)` loop calling `step_search()`
repeatedly, checking for `stop`/new `position` messages between each
depth. No thread, no `SharedArrayBuffer`, no headers — works on any
static host, including plain GitHub Pages.

The native CLI build (`main.cpp`) is completely unaffected — it still
uses the original threaded `search()` and gets full Lazy-SMP
parallelism across all your CPU cores in the terminal.

## Files here

- `engine.h` / `engine.cpp` — patched: adds the stepped-search API
  described above (a pure addition — nothing existing was changed or
  removed).
- `uci.h` / `uci.cpp` — patched: `execute()`, `web_mode`, `step()`, and
  `cmd_go` branches to the no-thread path when `web_mode == true`.
- `wasm_bridge.cpp` — new file, the Wasm entry point
  (`engine_init`/`send_uci_command`/`step_search`).
- `Makefile` — added a `web` target (single-threaded, no `-pthread`).
- `bitboard.h`, `evaluator.h`, `tt.h`, `main.cpp` are **unchanged** —
  drop these files into your existing tree alongside your originals.

## Build

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(`emcc`) on your `PATH`.

```bash
make web
# -> web/connect4.js, web/connect4.wasm
cp web/connect4.js web/connect4.wasm ../web/    # into the front-end folder
```

No special server configuration needed — single-threaded builds have
no cross-origin-isolation requirement, so any static host (GitHub
Pages, Netlify, nginx, `python -m http.server`, ...) works as-is.

## If you want the parallelism back later

The `Engine::search()` thread-based Lazy-SMP path is still there,
untouched, for the native CLI. If you ever want multiple search
threads in the browser too, it's a bigger step: you'd need to restore
`-pthread -s USE_PTHREADS=1` in the `Makefile`, keep the
`start_stepped_search`/`step_once()` path as a fallback for hosts
without cross-origin isolation, and have `engine-worker.js` detect
`self.crossOriginIsolated` at startup to choose which mode to drive.
Given how fast this engine already solves Connect-4 positions
single-threaded (see the `nps`/`nodes` figures in the dashboard), it's
unlikely to be worth the added complexity and hosting requirements.
