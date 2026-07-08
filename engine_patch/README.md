# Engine patch: WebAssembly bridge

I read through your actual source (`bitboard.h`, `engine.h`/`.cpp`, `uci.h`/`.cpp`,
`tt.h`, `evaluator.h`, `main.cpp`) before writing the front end. Two things there
don't match the prompt's assumptions, so I patched the engine rather than have
the JS silently guess wrong:

1. **No `send_uci_command` entry point exists yet.** `main.cpp` / `UCI::run()`
   is an interactive REPL that blocks on `std::getline(std::cin, ...)`. That
   works great in a terminal, but a Wasm module has no stdin to read from in a
   browser, and a blocking read loop can't be driven by `postMessage` events
   anyway. **Fix:** extracted the per-line command dispatch out of `run()`'s
   while-loop into a new public `UCI::execute(const std::string&)`, and added
   `wasm_bridge.cpp`, a ~30-line file exporting `engine_init()` and
   `send_uci_command(const char*)` via `EMSCRIPTEN_KEEPALIVE`/`extern "C"` —
   exactly the two calls the prompt assumed already existed.

2. **`print_info()`/`print_final_result()` don't print standard UCI text.**
   They print human-oriented lines with ANSI color codes and a decorative
   board (`format_score()`, box-drawing separators, etc.) — great for a
   terminal, not something you want to regex in JS. **Fix:** added a
   `web_mode` flag to `UCI` (`UCI(bool web_mode = false)`). When true,
   `print_info`/`print_final_result` instead print one clean, colorless line
   per event, built from the real fields on `SearchResult`/`SearchStats`
   (`score`, `mate_in`, `pv`, `nodes_explored`, `tt_hits`, ...):

   ```
   info depth 8 score cp 42 nodes 14205 nps 450000 time 31 ttpct 12.4 tthits 900 pv 3 4 2
   info depth 12 score mate 4 nodes 88210 nps 512000 time 172 ttpct 30.1 tthits 5000 pv 3 2 1
   bestmove 3
   ```

   Notes baked into the front end because of this:
   - `score` is this engine's own raw evaluation unit, **not centipawns**
     (`SCORE_WIN = 1,000,000`, `MATE_THRESHOLD = 900,000`, from `bitboard.h`).
     I kept the field name `cp` for familiarity but the JS treats it as a
     ~±1,500-range raw score (see `evaluator.h`'s term weights) and scales it
     with a `tanh` curve for the eval bar rather than assuming chess-style
     centipawns.
   - `mate_in` is already signed (`>0` = win in N plies, `<0` = lose in N),
     matching the comment in `engine.h` — passed straight through.
   - `nps` isn't tracked by `SearchStats` at all, so I added a
     `go_start_` timestamp (`std::chrono::steady_clock`) set at the top of
     `cmd_go`, and compute `nodes * 1000 / elapsed_ms` in `print_info`.
   - `uci`, `isready`→`readyok`, and `ucinewgame`'s ack were already plain
     text with no ANSI codes, so those needed no changes.

## Files here
- `uci.h` / `uci.cpp` — patched (diff-able against your original; only the
  additions described above, nothing else touched).
- `wasm_bridge.cpp` — new file, the actual Wasm entry point.
- `Makefile` — added a `web` target.
- Everything else (`bitboard.h`, `engine.h/.cpp`, `evaluator.h`, `tt.h`,
  `main.cpp`) is **unchanged** — drop these three files into your existing
  tree alongside your originals.

## Build

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(`emcc`) on your `PATH`.

```bash
make web
# -> web/connect4.js, web/connect4.wasm
cp web/connect4.js web/connect4.wasm  ../web/            # into the front-end folder
```

**Cross-origin isolation is required.** The engine's Lazy-SMP search and the
UCI search thread both use `std::thread`, which Emscripten maps onto real Web
Workers backed by `SharedArrayBuffer`. Browsers only allow `SharedArrayBuffer`
on pages served with:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Most static file servers (`python -m http.server`, GitHub Pages, etc.) don't
set these by default — the module will fail silently or throw
`SharedArrayBuffer is not defined` in the console. A minimal dev server that
sets them is in the front-end folder's `README.md`.

If you'd rather not deal with cross-origin isolation at all, rebuild with
`-s USE_PTHREADS=0` and change `Engine`'s Lazy-SMP path to run single-threaded
in web builds — the UCI protocol and this bridge don't care either way, only
`engine.cpp`'s internal thread count does.
