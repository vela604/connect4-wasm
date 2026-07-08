# Connect-4 Analysis Board

A Chess.com-style analysis dashboard for your multithreaded Connect-4 UCI
engine, running fully client-side via WebAssembly.

## What's here

```
index.html          Layout: eval bar / board / analytics sidebar
styles.css           Dark dashboard theme (see file header for the palette)
app.js               Game state, rendering, animations, engine bridge
engine-worker.js     Web Worker: loads the Wasm module, parses engine output
serve.js             Dev server that sets the headers Wasm threads need
engine_patch/        Changes to YOUR C++ so it can actually be called from JS
  README.md            — explains every change and why, build steps
  uci.h / uci.cpp       — patched (execute() + web_mode, diff-able)
  wasm_bridge.cpp       — new: the actual extern "C" entry point
  Makefile              — added a `web` target
```

## Why `engine_patch/` exists

I read your actual source before writing any front-end code. Two things
in the prompt's assumptions didn't match what's actually there:

- `Module.ccall('send_uci_command', ...)` didn't exist — `main.cpp`
  is an interactive REPL blocking on `std::cin`, which has nothing to
  read from inside a browser. `wasm_bridge.cpp` adds the real entry
  point, backed by a new public `UCI::execute()` extracted from the
  REPL loop (same dispatch logic, reusable).
- Your engine's `info`-equivalent output (`print_info`/`format_score`)
  is human-oriented text with ANSI color codes and a decorative board —
  not a machine-parseable line. I added a `web_mode` flag to `UCI` that
  makes it print a clean line instead, built from the real
  `SearchResult`/`SearchStats` fields (raw `score`, signed `mate_in`,
  `pv`, `nodes_explored`, etc.) — see `engine_patch/README.md` for the
  exact format and the reasoning behind the eval-bar scaling (your
  `score` is a raw unit, not centipawns; `SCORE_WIN = 1,000,000` per
  `bitboard.h`, not chess-style ±mate-in-100).

Everything in this top-level folder (worker's regex, eval-bar math,
etc.) is written against that patched, real output — not the generic
UCI format assumed in the original prompt.

## Running it

1. **Try it immediately, no build required.** Just serve this folder
   and open it:
   ```bash
   node serve.js
   # open http://localhost:8080
   ```
   Without `connect4.js`/`connect4.wasm` present, the worker posts
   `engine_missing` and `app.js` automatically switches to a small
   in-browser mock engine (streams plausible depth/eval/PV updates),
   so you can see the whole dashboard — animations, eval bar, PV
   clicking, history navigation — working end-to-end right away.

2. **Build the real engine** (see `engine_patch/README.md` for the
   full explanation):
   ```bash
   cd engine_patch
   # copy uci.h/uci.cpp/Makefile over your originals, add wasm_bridge.cpp
   make web
   cp web/connect4.js web/connect4.wasm ../
   ```
   Reload the page — the worker picks up the real module automatically
   (same code path, no front-end changes needed).

3. **Headers matter.** The engine's Lazy-SMP search uses real threads,
   which Emscripten maps onto Web Workers backed by `SharedArrayBuffer`.
   Browsers require:
   ```
   Cross-Origin-Opener-Policy: same-origin
   Cross-Origin-Embedder-Policy: require-corp
   ```
   `serve.js` sets both. If you deploy elsewhere (nginx, a CDN, GitHub
   Pages), you'll need to configure the same two headers there, or the
   Wasm module will fail to start once real threads are involved.

## Recommended workflow: GitHub Actions build + Netlify/Cloudflare Pages hosting

Building Emscripten on a phone (Termux/Acode) is slow and heavy (1–2GB
toolchain). The easier path: let GitHub's server build the Wasm, and
use a host that lets you set custom headers (GitHub Pages does not).

1. **Push this whole folder to a new GitHub repo** (from Acode's terminal):
   ```bash
   git init
   git add .
   git commit -m "Initial commit"
   git branch -M main
   git remote add origin https://github.com/<you>/<repo>.git
   git push -u origin main
   ```

2. **`.github/workflows/build-wasm.yml`** is already included. On every
   push that touches `engine_patch/**`, it:
   - installs Emscripten on GitHub's runner (nothing on your phone),
   - runs `make web` inside `engine_patch/`,
   - copies `connect4.js`/`connect4.wasm` into the repo root,
   - commits them back automatically.

   Check the **Actions** tab on GitHub after pushing to watch it build.
   You can also trigger it manually via "Run workflow" there.

3. **Connect the repo to Netlify or Cloudflare Pages** (both free, both
   support the `_headers` file included here — GitHub Pages doesn't
   support custom headers at all, so it won't work for the threaded
   build):
   - Netlify: “Add new site” → “Import from GitHub” → pick the repo →
     leave build command empty, publish directory `/`.
   - Cloudflare Pages: “Create a project” → connect GitHub repo → same,
     no build command needed (Actions already built the wasm files).

   Either one auto-redeploys whenever Actions pushes new
   `connect4.js`/`connect4.wasm` to `main`. Open the resulting URL
   directly on your phone — no Acode terminal needed at that point.

### Alternative: everything local via Acode/Termux

If you'd rather not use a second hosting service, after step 2's
Action has committed `connect4.js`/`connect4.wasm`:
```bash
git pull
node serve.js
```
Then open `http://localhost:8080` in your phone's browser. `serve.js`
already sets the COOP/COEP headers, so this works standalone with no
external host — just less convenient than a public URL if you want to
share it with others.

### If you'd rather build with `emcc` directly in Acode/Termux

Possible, but expect it to be slow and use real storage:
```bash
pkg install python nodejs git cmake   # Termux packages
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
cd ../engine_patch
make web
```
Emscripten doesn't officially publish prebuilt Android/aarch64
binaries for every release, so `emsdk install latest` may fall back to
building LLVM from source on-device, which can take a very long time
on a phone. The GitHub Actions route avoids this entirely.

## Notes on the design choices


- **Eval bar / score scaling.** The engine's `score` is a raw
  evaluation unit from `evaluator.h`'s term weights (roughly a few
  hundred to ~1000 in normal positions), not centipawns, and
  `SCORE_WIN = 1,000,000` / `MATE_THRESHOLD = 900,000`. The bar maps
  score → fill % with `50 + 50·tanh(score/450)` so it saturates
  smoothly near forced wins/losses rather than needing a hard cutoff,
  and switches to a hard 0%/100% + `M<n>` label once `mate_in != 0`.
- **Perspective.** `score`/`mate_in` are reported from the point of
  view of whichever side is to move in the searched position
  (negamax convention) — `app.js` flips the sign based on ply parity
  before feeding the eval bar so it's always Player-1-relative, while
  the analytics panel shows the engine's own raw (mover-relative)
  number, which is what `nodes`/`nps`/`depth` are inherently tied to.
- **Move format.** Columns are `0`-`6`, sent as a space-joined string
  (`"3 4 2"`) matching `apply_moves()`'s single-character token parser
  exactly — no translation layer needed on either side.
- **Stale-result guarding.** Every `search_info` message is tagged
  with the ply it was requested for; if the user navigates to a
  different position while a search is in flight, late-arriving
  updates for the old position are silently dropped instead of
  flashing an eval for the wrong board state.
