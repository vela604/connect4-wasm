// ============================================================
// engine-worker.js
//
// Runs the Connect-4 UCI engine (compiled to WebAssembly via
// Emscripten, single-threaded build) off the main thread so an
// "infinite" search never freezes the UI. Talks to the main thread
// only via postMessage.
//
// This build has NO std::thread / SharedArrayBuffer dependency at
// all, so it needs no Cross-Origin-Opener-Policy / Cross-Origin-
// Embedder-Policy headers and works on any static host. Instead of
// the C++ side running "go infinite" on a background thread, JS
// itself drives the search one depth at a time by calling the
// exported step_search() in a loop (see driveSearch() below), with a
// setTimeout(...,0) between calls so incoming 'stop'/'position'
// messages always get a chance to run before the next depth starts.
//
// Expects connect4.js / connect4.wasm — the output of the `web`
// Makefile target in engine_patch/ — to sit next to this file:
//
//   emcc -std=c++17 -O3 -DNDEBUG \
//        -s EXPORTED_FUNCTIONS='["_engine_init","_send_uci_command","_step_search","_main"]' \
//        -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
//        -s MODULARIZE=1 -s EXPORT_NAME='Connect4Module' \
//        -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=worker \
//        engine.cpp uci.cpp wasm_bridge.cpp -o connect4.js
//
// If connect4.js 404s (not built yet) or fails to init, we post
// 'engine_missing' and app.js transparently switches to its
// in-browser mock engine so the dashboard is still fully demoable.
// ============================================================

let sendCommand = null;
let stepFn = null;
let ready = false;
const pending = [];

// Matches the clean, ANSI-free line UCI::print_info() emits when the
// engine is constructed with web_mode = true (see engine_patch/uci.cpp):
//   info depth 8 score cp 42 nodes 14205 nps 450000 time 31 ttpct 12.4 tthits 900 pv 3 4 2
//   info depth 12 score mate 4 nodes 88210 nps 512000 time 172 ttpct 30.1 tthits 5000 pv 3 2 1
const INFO_RE = /^info depth (\d+) score (?:cp (-?\d+)|mate (-?\d+)) nodes (\d+) nps (\d+) time (\d+)(?:.*?pv ([\d\s]+))?/;
const BESTMOVE_RE = /^bestmove (\d+|none)/;

function handleEngineLine(line) {
  if (!line) return;
  line = line.trim();
  if (!line) return;

  const m = INFO_RE.exec(line);
  if (m) {
    const depth = parseInt(m[1], 10);
    const isMate = m[3] !== undefined;
    const scoreCp = isMate ? 0 : parseInt(m[2], 10);
    const mateIn = isMate ? parseInt(m[3], 10) : 0;
    const nodes = parseInt(m[4], 10);
    const nps = parseInt(m[5], 10);
    const timeMs = parseInt(m[6], 10);
    const pv = m[7] ? m[7].trim().split(/\s+/).map(Number) : [];
    postMessage({ type: 'search_info', data: { depth, scoreCp, mateIn, nodes, nps, timeMs, pv } });
    return;
  }

  const bm = BESTMOVE_RE.exec(line);
  if (bm) {
    postMessage({ type: 'bestmove', data: bm[1] === 'none' ? null : parseInt(bm[1], 10) });
    return;
  }

  if (line === 'readyok') { postMessage({ type: 'ready' }); return; }

  // uciok / ucinewgame ack / unknown-command errors, etc. — surfaced
  // for the log panel rather than silently dropped.
  postMessage({ type: 'log', data: line });
}

function flushPending() {
  while (pending.length) send(pending.shift());
}

function send(cmd) {
  if (!ready || !sendCommand) { pending.push(cmd); return; }
  try {
    sendCommand(cmd);
  } catch (err) {
    postMessage({ type: 'log', data: '[bridge error] ' + err });
  }
}

// Drives an in-progress "go infinite" one depth at a time. Safe to
// call more than once concurrently (e.g. right after a 'stop' from an
// older position) — step_search() returns 0 as soon as UCI::searching_
// is false, so any stale loop quietly ends itself on its next tick.
function driveSearch() {
  if (!stepFn) return;
  let cont = false;
  try {
    cont = stepFn() === 1;
  } catch (err) {
    postMessage({ type: 'log', data: '[step error] ' + err });
    return;
  }
  if (cont) setTimeout(driveSearch, 0);
}

function loadEngine() {
  self.Module = {
    print: handleEngineLine,
    printErr: (line) => postMessage({ type: 'log', data: '[stderr] ' + line }),
    onRuntimeInitialized() {
      sendCommand = self.Module.cwrap('send_uci_command', null, ['string']);
      stepFn = self.Module.cwrap('step_search', 'number', []);
      self.Module.ccall('engine_init', null, [], []);
      ready = true;
      postMessage({ type: 'engine_ready' });
      flushPending();
    },
  };

  // Catch every way this can go wrong: a synchronous throw (e.g. the
  // file 404s), an async rejection during module instantiation, or an
  // uncaught runtime error — any of these now reports 'engine_missing'
  // instead of leaving the main thread waiting with no signal at all.
  self.addEventListener('error', (e) => {
    postMessage({ type: 'engine_missing', data: 'worker error: ' + (e.message || e) });
  });
  self.addEventListener('unhandledrejection', (e) => {
    postMessage({ type: 'engine_missing', data: 'unhandled rejection: ' + (e.reason && e.reason.message ? e.reason.message : e.reason) });
  });

  try {
    importScripts('./connect4.js');
    // MODULARIZE=1 + EXPORT_NAME=Connect4Module means connect4.js defines
    // a factory function rather than eagerly creating the module. Pass
    // our overrides in and grab the resolved instance.
    // eslint-disable-next-line no-undef
    Connect4Module(self.Module)
      .then((m) => { self.Module = m; })
      .catch((err) => postMessage({ type: 'engine_missing', data: 'module init failed: ' + String(err) }));
  } catch (err) {
    postMessage({ type: 'engine_missing', data: String(err && err.message ? err.message : err) });
  }
}

self.onmessage = (e) => {
  const msg = e.data || {};
  switch (msg.cmd) {
    case 'init':
      loadEngine();
      break;
    case 'position': {
      const movesStr = (msg.moves || []).join(' ');
      send('stop');
      send(movesStr.length ? `position startpos moves ${movesStr}` : 'position startpos');
      send('go infinite');
      driveSearch();
      break;
    }
    case 'stop':
      send('stop');
      break;
    default:
      break;
  }
};
