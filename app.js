// ============================================================
// Connect-4 Analysis Board — main thread
//
// Owns all game/UI state and talks to the engine exclusively
// through engine-worker.js (a Web Worker) via postMessage, so
// the "go infinite" search never blocks rendering or input.
//
// Column convention matches the C++ engine's bitboard.h exactly:
// columns are 0-6, row 0 is the bottom row. The move list sent to
// the engine ("position startpos moves 3 4 2 ...") is just the
// space-joined column sequence — identical to what UCI::cmd_position
// / apply_moves() parses.
// ============================================================

const ROWS = 6;
const COLS = 7;

// ---------- game state ----------
let moves = [];        // full played sequence (branch is truncated on new move)
let viewPly = 0;        // 0..moves.length — which position is currently displayed
let flipped = false;
let engineOn = true;
let hoverCol = null;
let currentHeights = Array(COLS).fill(0);
let currentSearchPly = 0; // which viewPly the last 'position' sent to the engine corresponds to

// ---------- engine bridge state ----------
let worker = null;
let usingMock = false;
let mockTimer = null;

// ---------- DOM refs ----------
const el = {
  boardGrid: document.getElementById('boardGrid'),
  columnOverlay: document.getElementById('columnOverlay'),
  boardGridWrap: document.querySelector('.board-grid-wrap'),
  bestMoveArrow: document.getElementById('bestMoveArrow'),
  resultBanner: document.getElementById('resultBanner'),
  chipP1: document.getElementById('chipP1'),
  chipP2: document.getElementById('chipP2'),
  evalFill: document.getElementById('evalFill'),
  evalLabel: document.getElementById('evalLabel'),
  depthBadgeValue: document.getElementById('depthBadgeValue'),
  evalStripScore: document.getElementById('evalStripScore'),
  evalStripPv: document.getElementById('evalStripPv'),
  historyList: document.getElementById('historyList'),
  logBox: document.getElementById('logBox'), // optional; may not exist in this layout
  statusLed: document.getElementById('statusLed'),
  statusText: document.getElementById('statusText'),
  mockBannerWrap: document.getElementById('mockBannerWrap'),
  engineToggle: document.getElementById('engineToggle'),
  btnFlip: document.getElementById('btnFlip'),
  btnBack: document.getElementById('btnBack'),
  btnForward: document.getElementById('btnForward'),
  btnReset: document.getElementById('btnReset'),
};

let ghostEl = null;

// ============================================================
// Board helpers (pure functions over the move list)
// ============================================================

function boardFromMoves(list) {
  const board = Array.from({ length: ROWS }, () => Array(COLS).fill(0));
  const heights = Array(COLS).fill(0);
  let lastPos = null;
  for (let i = 0; i < list.length; i++) {
    const col = list[i];
    const row = heights[col];
    if (row >= ROWS) continue; // defensive; shouldn't happen with valid input
    board[row][col] = (i % 2 === 0) ? 1 : 2;
    heights[col]++;
    lastPos = { row, col };
  }
  return { board, heights, lastPos };
}

function checkWinner(board) {
  const dirs = [[0, 1], [1, 0], [1, 1], [1, -1]];
  for (let r = 0; r < ROWS; r++) {
    for (let c = 0; c < COLS; c++) {
      const v = board[r][c];
      if (!v) continue;
      for (const [dr, dc] of dirs) {
        let count = 1;
        for (let k = 1; k < 4; k++) {
          const rr = r + dr * k, cc = c + dc * k;
          if (rr < 0 || rr >= ROWS || cc < 0 || cc >= COLS || board[rr][cc] !== v) break;
          count++;
        }
        if (count >= 4) return v;
      }
    }
  }
  return 0;
}

function sideToMove(ply) { return ply % 2 === 0 ? 1 : 2; }

// ============================================================
// Rendering
// ============================================================

function render() {
  const slice = moves.slice(0, viewPly);
  const { board, heights, lastPos } = boardFromMoves(slice);
  currentHeights = heights;

  const winner = checkWinner(board);
  const isDraw = !winner && slice.length === ROWS * COLS;
  const gameOver = !!winner || isDraw;

  renderBoard(board, lastPos);
  renderOverlay(heights, gameOver);
  renderBanner(winner, isDraw);
  renderChips(slice.length, gameOver);
  renderHistory();
  hideGhost();
  el.btnBack.disabled = viewPly === 0;
  el.btnForward.disabled = viewPly === moves.length;
}

function renderBoard(board, lastPos) {
  el.boardGrid.innerHTML = '';
  const colOrder = flipped ? [6, 5, 4, 3, 2, 1, 0] : [0, 1, 2, 3, 4, 5, 6];

  for (let displayRow = 0; displayRow < ROWS; displayRow++) {
    const realRow = (ROWS - 1) - displayRow; // top of screen = highest row index
    for (const realCol of colOrder) {
      const cell = document.createElement('div');
      cell.className = 'cell';
      cell.dataset.col = String(realCol);
      cell.dataset.displayRow = String(displayRow);
      const v = board[realRow][realCol];
      if (v) {
        const piece = document.createElement('div');
        const isLast = lastPos && lastPos.row === realRow && lastPos.col === realCol;
        piece.className = `piece p${v}` + (isLast ? '' : ' no-anim');
        if (isLast) {
          const rowsFromTop = (ROWS - 1) - realRow;
          piece.style.setProperty('--fall', `${-(rowsFromTop + 1) * 112}%`);
        }
        cell.appendChild(piece);
      }
      el.boardGrid.appendChild(cell);
    }
  }
}

function renderOverlay(heights, gameOver) {
  el.columnOverlay.innerHTML = '';
  const colOrder = flipped ? [6, 5, 4, 3, 2, 1, 0] : [0, 1, 2, 3, 4, 5, 6];
  for (const realCol of colOrder) {
    const full = heights[realCol] >= ROWS;
    const hit = document.createElement('div');
    hit.className = 'col-hit' + (full || gameOver ? ' full' : '');
    hit.dataset.col = String(realCol);
    hit.addEventListener('mouseenter', () => {
      if (!full && !gameOver) showGhost(realCol);
    });
    hit.addEventListener('mouseleave', hideGhost);
    hit.addEventListener('click', () => {
      if (!full && !gameOver) playMove(realCol);
    });
    el.columnOverlay.appendChild(hit);
  }
}

function showGhost(realCol) {
  if (!ghostEl) {
    ghostEl = document.createElement('div');
    ghostEl.className = 'piece ghost no-anim';
    ghostEl.style.position = 'absolute';
    el.boardGridWrap.appendChild(ghostEl);
  }
  const landingRow = currentHeights[realCol];
  if (landingRow >= ROWS) { hideGhost(); return; }
  const displayCol = flipped ? (6 - realCol) : realCol;
  const displayRowFromTop = (ROWS - 1) - landingRow;
  const cw = 100 / COLS, ch = 100 / ROWS;
  ghostEl.style.left = `${displayCol * cw}%`;
  ghostEl.style.top = `${displayRowFromTop * ch}%`;
  ghostEl.style.width = `${cw}%`;
  ghostEl.style.height = `${ch}%`;
  ghostEl.className = `piece ghost no-anim p${sideToMove(viewPly)}`;
  ghostEl.style.display = 'block';
}

function hideGhost() {
  if (ghostEl) ghostEl.style.display = 'none';
  hoverCol = null;
}

function renderBanner(winner, isDraw) {
  const b = el.resultBanner;
  if (winner) {
    b.className = 'result-banner show ' + (winner === 1 ? 'win' : 'loss');
    b.textContent = `Player ${winner} wins!`;
  } else if (isDraw) {
    b.className = 'result-banner show draw';
    b.textContent = "It's a draw.";
  } else {
    b.className = 'result-banner';
    b.textContent = '';
  }
}

function renderChips(plyCount, gameOver) {
  const active = gameOver ? 0 : sideToMove(plyCount);
  el.chipP1.classList.toggle('active', active === 1);
  el.chipP2.classList.toggle('active', active === 2);
}

function renderHistory() {
  const list = el.historyList;
  list.innerHTML = '';

  const startRow = document.createElement('div');
  startRow.className = 'history-row' + (viewPly === 0 ? ' active' : '');
  startRow.innerHTML = `<span class="ply">–</span><span>Start position</span>`;
  startRow.addEventListener('click', () => goToPly(0));
  list.appendChild(startRow);

  moves.forEach((col, i) => {
    const ply = i + 1;
    const mover = i % 2 === 0 ? 1 : 2;
    const row = document.createElement('div');
    row.className = 'history-row' + (viewPly === ply ? ' active' : '');
    row.innerHTML = `<span class="ply">${ply}.</span><span class="dot p${mover}"></span><span>Column ${col}</span>`;
    row.addEventListener('click', () => goToPly(ply));
    list.appendChild(row);
  });
}

// ============================================================
// Game actions
// ============================================================

function playMove(col) {
  if (viewPly < moves.length) moves = moves.slice(0, viewPly); // branch: discard redone future
  moves.push(col);
  viewPly = moves.length;
  render();
  requestEngineAnalysis();
}

function goToPly(ply) {
  viewPly = Math.max(0, Math.min(moves.length, ply));
  render();
  requestEngineAnalysis();
}

// Non-destructive step navigation (like chess.com's back/forward
// arrows). Unlike the old "Take Back" button, this never deletes
// anything from `moves` — it only moves the view pointer, so you can
// always step forward again afterwards.
function stepBack() { goToPly(viewPly - 1); }
function stepForward() { goToPly(viewPly + 1); }

function resetBoard() {
  moves = [];
  viewPly = 0;
  render();
  requestEngineAnalysis();
}

function flipBoard() {
  flipped = !flipped;
  render();
}

function playPvLine(pv, uptoIndex) {
  const line = pv.slice(0, uptoIndex + 1);
  moves = moves.slice(0, viewPly).concat(line);
  viewPly = moves.length;
  render();
  requestEngineAnalysis();
}

// ============================================================
// Engine bridge (real Wasm worker, or mock fallback)
// ============================================================

function requestEngineAnalysis() {
  currentSearchPly = viewPly;
  clearLiveStats();

  const slice = moves.slice(0, viewPly);
  const { board } = boardFromMoves(slice);
  const gameOver = checkWinner(board) || slice.length === ROWS * COLS;

  if (!engineOn || gameOver) {
    if (usingMock) stopMock(); else worker && worker.postMessage({ cmd: 'stop' });
    setStatus(engineOn ? 'on' : 'off', gameOver ? 'Game over' : 'Paused');
    return;
  }

  if (usingMock) {
    startMock(slice);
  } else if (worker) {
    worker.postMessage({ cmd: 'position', moves: slice });
    setStatus('busy', 'Searching…');
  }
}

function clearLiveStats() {
  el.depthBadgeValue.textContent = '–';
  el.evalStripScore.textContent = '–';
  el.evalStripPv.innerHTML = '<span class="pv-placeholder">Waiting for engine…</span>';
  hideBestMoveArrow();
}

function onSearchInfo(data, forPly) {
  if (forPly !== currentSearchPly) return; // stale info from a superseded position
  setStatus('busy', 'Searching…');

  const side = sideToMove(forPly); // 1 or 2 — whose move the engine is evaluating
  const sign = side === 1 ? 1 : -1; // flip to Player-1 perspective for the eval bar
  const scoreP1 = data.scoreCp * sign;
  const mateP1 = data.mateIn * sign;

  updateEvalUI(scoreP1, mateP1, data.depth);

  el.depthBadgeValue.textContent = data.depth;
  el.evalStripScore.textContent = formatScoreLabel(data.scoreCp, data.mateIn);
  el.evalStripScore.style.color = data.scoreCp >= 0 || data.mateIn > 0 ? 'var(--win)' : 'var(--loss)';

  el.evalStripPv.innerHTML = '';
  data.pv.forEach((col, i) => {
    const chip = document.createElement('span');
    chip.className = 'pv-move';
    chip.textContent = `${i + 1}.${col}`;
    chip.title = 'Play out this predicted line';
    chip.addEventListener('click', () => playPvLine(data.pv, i));
    el.evalStripPv.appendChild(chip);
  });

  showBestMoveArrow(data.pv[0]);
}

function showBestMoveArrow(col) {
  if (col === undefined || col === null) { hideBestMoveArrow(); return; }
  const cell = el.boardGrid.querySelector(`.cell[data-col="${col}"][data-display-row="0"]`);
  if (!cell) { hideBestMoveArrow(); return; }
  const cellRect = cell.getBoundingClientRect();
  const wrapRect = el.boardGridWrap.getBoundingClientRect();
  const centerPx = (cellRect.left - wrapRect.left) + cellRect.width / 2;
  el.bestMoveArrow.style.left = `${centerPx}px`;
  el.bestMoveArrow.style.width = '0px';
  el.bestMoveArrow.classList.add('show');
}

function hideBestMoveArrow() {
  el.bestMoveArrow.classList.remove('show');
}

function formatScoreLabel(scoreCp, mateIn) {
  if (mateIn !== 0) return (mateIn > 0 ? 'M' : '-M') + Math.abs(mateIn);
  const v = (scoreCp / 100).toFixed(1);
  return (scoreCp >= 0 ? '+' : '') + v;
}

function updateEvalUI(scoreP1, mateP1, depth) {
  let pct, label;
  if (mateP1 !== 0) {
    pct = mateP1 > 0 ? 100 : 0;
    label = (mateP1 > 0 ? 'M' : '-M') + Math.abs(mateP1);
  } else {
    pct = 50 + 50 * Math.tanh(scoreP1 / 450);
    label = formatScoreLabel(scoreP1, 0);
  }
  el.evalFill.style.height = `${pct}%`;
  const labelTop = Math.min(92, Math.max(4, 100 - pct));
  el.evalLabel.style.top = `${labelTop}%`;
  el.evalLabel.style.color = pct < 50 ? '#e9ecf4' : '#0a0c11';
  el.evalLabel.textContent = label;
}

function setStatus(state, text) {
  el.statusLed.className = 'status-led ' + (state === 'busy' ? 'busy' : state === 'on' ? 'on' : 'off');
  el.statusText.textContent = text || (state === 'busy' ? 'Searching…' : state === 'on' ? 'Ready' : 'Idle');
}

function appendLog(text) {
  if (!el.logBox) { console.log('[connect4]', text); return; }
  const line = document.createElement('div');
  line.textContent = text;
  el.logBox.prepend(line);
  while (el.logBox.children.length > 60) el.logBox.removeChild(el.logBox.lastChild);
}

// ---------- real worker wiring ----------

function initWorker() {
  worker = new Worker('engine-worker.js');
  let settled = false; // true once we've heard engine_ready or engine_missing

  worker.onmessage = (e) => {
    const { type, data } = e.data;
    switch (type) {
      case 'engine_ready':
        settled = true;
        setStatus('on', 'Engine ready');
        appendLog('[engine] ready');
        requestEngineAnalysis();
        break;
      case 'engine_missing':
        settled = true;
        appendLog('[engine] connect4.js not found or failed to load — falling back to mock engine (' + data + ')');
        switchToMock();
        break;
      case 'search_info':
        onSearchInfo(data, currentSearchPly);
        break;
      case 'bestmove':
        appendLog(`bestmove ${data === null ? 'none' : data}`);
        break;
      case 'log':
        appendLog(data);
        break;
      default:
        break;
    }
  };
  worker.onerror = (err) => {
    settled = true;
    appendLog('[worker error] ' + err.message);
    switchToMock();
  };
  worker.postMessage({ cmd: 'init' });
  setStatus('off', 'Connecting to engine…');

  // Safety net: if neither 'engine_ready' nor 'engine_missing' arrives
  // within a few seconds (e.g. the Wasm module hung during a silent
  // failure this code doesn't otherwise catch), don't leave the UI
  // stuck forever — fall back to the mock engine automatically.
  setTimeout(() => {
    if (!settled) {
      appendLog('[engine] no response after 7s — falling back to mock engine');
      switchToMock();
    }
  }, 7000);
}

// ---------- mock fallback (so the dashboard works before connect4.js is built) ----------

function switchToMock() {
  usingMock = true;
  el.mockBannerWrap.style.display = 'block';
  setStatus('on', 'Mock engine ready');
  requestEngineAnalysis();
}

function seedFromMoves(list) {
  let h = 17;
  for (const c of list) h = (h * 31 + c + 7) % 100000;
  return h;
}

function stopMock() {
  if (mockTimer) { clearInterval(mockTimer); mockTimer = null; }
}

function startMock(slice) {
  stopMock();
  const seed = seedFromMoves(slice);
  const side = sideToMove(slice.length);
  let baseScore = ((seed % 700) - 350) * (side === 1 ? 1 : 1); // mover-perspective baseline
  let depth = 1;
  let nodes = 0;
  const searchPlyAtStart = viewPly;

  mockTimer = setInterval(() => {
    if (searchPlyAtStart !== viewPly) { stopMock(); return; }
    depth = Math.min(34, depth + 1);
    nodes += Math.floor(8000 + Math.random() * 40000 * depth);
    const wobble = (Math.random() - 0.5) * (260 / depth);
    const scoreCp = Math.round(baseScore + wobble);
    const nps = Math.floor(350000 + Math.random() * 400000);
    const pv = Array.from({ length: Math.min(6, depth) }, () => Math.floor(Math.random() * COLS));
    let mateIn = 0;
    if (depth >= 30 && Math.abs(scoreCp) > 300 && Math.random() < 0.15) {
      mateIn = (scoreCp > 0 ? 1 : -1) * (Math.floor(Math.random() * 6) + 1);
    }
    onSearchInfo({ depth, scoreCp: mateIn ? 0 : scoreCp, mateIn, nodes, nps, timeMs: depth * 90, pv }, searchPlyAtStart);
    if (depth >= 34) stopMock();
  }, 320);
}

// ============================================================
// Controls
// ============================================================

el.btnFlip.addEventListener('click', flipBoard);
el.btnBack.addEventListener('click', stepBack);
el.btnForward.addEventListener('click', stepForward);
el.btnReset.addEventListener('click', resetBoard);
el.engineToggle.addEventListener('click', () => {
  engineOn = !engineOn;
  el.engineToggle.classList.toggle('on', engineOn);
  requestEngineAnalysis();
});

// ============================================================
// Boot
// ============================================================

render();
initWorker();
