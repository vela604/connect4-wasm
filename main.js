// main.js

const ROWS = 6;
const COLS = 7;
let gameHistory = []; // Stores column numbers played (e.g., [3, 4, 2])
let engineEnabled = true;

// DOM Nodes
const boardContainer = document.getElementById('board-container');
const evalFill = document.getElementById('eval-fill');
const evalScore = document.getElementById('eval-score');
const statDepth = document.getElementById('stat-depth');
const statNps = document.getElementById('stat-nps');
const statNodes = document.getElementById('stat-nodes');
const pvContainer = document.getElementById('pv-container');
const moveHistoryTree = document.getElementById('move-history-tree');
const engineStatusText = document.getElementById('engine-status-text');
const engineIndicator = document.getElementById('engine-status-indicator');

// Worker Initialization
const worker = new Worker('engine-worker.js');

worker.onmessage = function(e) {
    const msg = e.data;
    
    if (msg.type === 'ready') {
        engineStatusText.textContent = "Engine Ready";
        engineIndicator.classList.replace('bg-blue-500', 'bg-green-500');
        engineIndicator.classList.remove('animate-pulse');
        requestAnalysis();
    } 
    else if (msg.type === 'info' && engineEnabled) {
        updateAnalyticsUI(msg);
    }
    else if (msg.type === 'bestmove') {
        if (msg.move === null) {
            // Game Over or Interrupted
            pvContainer.textContent = "Game Finished / Forced Outcome Reached";
            evalScore.textContent = "Game Over";
        }
    }
};

// ─── INITIALIZATION ──────────────────────────────────────────

function createGrid() {
    boardContainer.innerHTML = '';
    
    for (let r = ROWS - 1; r >= 0; r--) {
        for (let c = 0; c < COLS; c++) {
            const cell = document.createElement('div');
            cell.className = 'cell aspect-square bg-slate-900 rounded-full m-1 shadow-inner relative cursor-pointer hover:bg-slate-800 transition-colors duration-150';
            cell.dataset.row = r;
            cell.dataset.col = c;
            cell.onclick = () => handleColumnClick(c);
            boardContainer.appendChild(cell);
        }
    }
}

// ─── GAME LOGIC & STATE ──────────────────────────────────────

function getBoardState() {
    const board = Array(ROWS).fill(null).map(() => Array(COLS).fill(0));
    const colCounts = Array(COLS).fill(0);

    for (let i = 0; i < gameHistory.length; i++) {
        const col = gameHistory[i];
        const row = colCounts[col];
        if (row < ROWS) {
            board[row][col] = (i % 2 === 0) ? 1 : 2; // 1 = Yellow (P1), 2 = Red (P2)
            colCounts[col]++;
        }
    }
    return { board, colCounts };
}

function handleColumnClick(col) {
    const { colCounts } = getBoardState();
    if (colCounts[col] >= ROWS) return; // Column full

    gameHistory.push(col);
    renderState();
    updateHistoryUI();
    requestAnalysis();
}

function renderState() {
    const { board } = getBoardState();
    const cells = boardContainer.getElementsByClassName('cell');

    for (let r = ROWS - 1; r >= 0; r--) {
        for (let c = 0; c < COLS; c++) {
            const cellIndex = (ROWS - 1 - r) * COLS + c;
            const cell = cells[cellIndex];
            const player = board[r][c];

            // Clear previous piece styling
            cell.innerHTML = '';

            if (player !== 0) {
                const piece = document.createElement('div');
                piece.className = `w-[88%] h-[88%] rounded-full absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 shadow-md animate-drop`;
                if (player === 1) {
                    piece.classList.add('bg-amber-400', 'border-2', 'border-amber-500');
                } else {
                    piece.classList.add('bg-rose-500', 'border-2', 'border-rose-600');
                }
                cell.appendChild(piece);
            }
        }
    }
}

function requestAnalysis() {
    if (!engineEnabled) return;
    
    // Clear old data line instantly so user knows it's reloading
    pvContainer.textContent = "Calculating...";
    
    worker.postMessage({
        cmd: 'position',
        moves: gameHistory
    });
}

// ─── UI CONTROLS ─────────────────────────────────────────────

function updateAnalyticsUI(data) {
    statDepth.textContent = data.depth || '-';
    statNodes.textContent = data.nodes ? data.nodes.toLocaleString() : '0';
    statNps.textContent = data.nps ? data.nps.toLocaleString() : '-';
    evalScore.textContent = data.scoreText || '0.0';

    // Update Eval Bar UI
    let percentage = 50; // default draw
    if (data.scoreCp !== undefined) {
        // Clamp score between -1000 and +1000 centipawns for UI scaling
        let clamped = Math.max(-1000, Math.min(1000, data.scoreCp));
        percentage = 50 + (clamped / 20); // Maps -1000 to 0%, +1000 to 100%
    }
    evalFill.style.height = `${percentage}%`;

    // Render PV Line
    if (data.pv && data.pv.length > 0) {
        pvContainer.innerHTML = '';
        data.pv.forEach(move => {
            const badge = document.createElement('span');
            badge.className = 'px-2.5 py-1 bg-slate-800 border border-slate-700 rounded text-sm font-semibold text-slate-300';
            badge.textContent = `Col ${move}`;
            pvContainer.appendChild(badge);
        });
    } else {
        pvContainer.textContent = '-';
    }
}

function updateHistoryUI() {
    moveHistoryTree.innerHTML = '';
    
    for (let i = 0; i < gameHistory.length; i += 2) {
        const row = document.createElement('div');
        row.className = 'grid grid-cols-3 gap-2 px-2 py-1 text-sm border-b border-slate-800/50 hover:bg-slate-800/20';
        
        const turnLabel = document.createElement('span');
        turnLabel.className = 'text-slate-500 font-medium';
        turnLabel.textContent = `${Math.floor(i / 2) + 1}.`;

        const move1 = createHistoryBtn(gameHistory[i], i);
        let move2 = null;
        if (i + 1 < gameHistory.length) {
            move2 = createHistoryBtn(gameHistory[i + 1], i + 1);
        }

        row.appendChild(turnLabel);
        row.appendChild(move1);
        if (move2) row.appendChild(move2);
        
        moveHistoryTree.appendChild(row);
    }
    moveHistoryTree.scrollTop = moveHistoryTree.scrollHeight;
}

function createHistoryBtn(col, index) {
    const btn = document.createElement('button');
    btn.className = 'w-8 text-center bg-slate-700/80 hover:bg-slate-600 rounded text-slate-300';
    btn.textContent = col;
    btn.onclick = () => {
        gameHistory = gameHistory.slice(0, index + 1);
        renderState();
        updateHistoryUI();
        requestAnalysis();
    };
    return btn;
}

// Event Listeners for Controls
document.getElementById('btn-undo').addEventListener('click', () => {
    if (gameHistory.length > 0) {
        gameHistory.pop();
        renderState();
        updateHistoryUI();
        requestAnalysis();
    }
});

document.getElementById('btn-reset').addEventListener('click', () => {
    gameHistory = [];
    renderState();
    updateHistoryUI();
    requestAnalysis();
});

document.getElementById('engine-toggle').addEventListener('change', (e) => {
    engineEnabled = e.target.checked;
    if (engineEnabled) {
        requestAnalysis();
    } else {
        worker.postMessage({ cmd: 'stop' });
        statDepth.textContent = '-';
        statNps.textContent = '-';
        evalScore.textContent = '0.0';
        pvContainer.textContent = 'Analysis Paused';
        evalFill.style.height = '50%';
    }
});

// App Startup
createGrid();
renderState();
