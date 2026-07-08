const ROWS = 6;
const COLS = 7;
let currentMoves = [];
let viewMoves = []; // Allows looking at history without erasing future moves until a new move is played
let flipped = false;
let engineEnabled = true;

// Initialize Web Worker
const engineWorker = new Worker('engine-worker.js');

// DOM Elements
const gridEl = document.getElementById('grid');
const evalFill = document.getElementById('eval-fill');
const evalText = document.getElementById('eval-text');
const historyEl = document.getElementById('move-history');
const toggleEngineBtn = document.getElementById('toggle-engine');

// Setup the interactive grid HTML
function initBoard() {
    gridEl.innerHTML = '';
    // Create columns to allow full-column hover interactions
    for (let c = 0; c < COLS; c++) {
        const colDiv = document.createElement('div');
        colDiv.className = 'col-interactive flex flex-col gap-2 py-1';
        colDiv.dataset.col = c;
        
        // Create rows within columns (Top to bottom: row 5 down to 0)
        for (let r = ROWS - 1; r >= 0; r--) {
            const cell = document.createElement('div');
            cell.className = 'c4-cell empty';
            cell.id = `cell-${c}-${r}`;
            colDiv.appendChild(cell);
        }
        
        colDiv.addEventListener('click', () => handleMove(c));
        gridEl.appendChild(colDiv);
    }
    renderBoard();
}

// Compute the current 2D board state from a sequence of moves
function getBoardState(moves) {
    let state = Array.from({ length: COLS }, () => []);
    moves.forEach((col, idx) => {
        const player = (idx % 2 === 0) ? 'p1' : 'p2';
        state[col].push(player);
    });
    return state;
}

// Draw the board based on `viewMoves`
function renderBoard(animateLast = false) {
    const state = getBoardState(viewMoves);
    const currentPlayer = (viewMoves.length % 2 === 0) ? 'hover-p1' : 'hover-p2';

    // Clear previews and update column hover states
    document.querySelectorAll('.col-interactive').forEach(col => {
        col.classList.remove('hover-p1', 'hover-p2');
        col.classList.add(currentPlayer);
    });

    for (let c = 0; c < COLS; c++) {
        const colState = state[c];
        const actualCol = flipped ? (COLS - 1 - c) : c;
        const colDiv = document.querySelector(`.col-interactive[data-col="${actualCol}"]`);
        
        // Remove old tokens and classes
        colDiv.querySelectorAll('.c4-cell').forEach(cell => {
            cell.innerHTML = '';
            cell.classList.add('empty');
            cell.classList.remove('preview-target');
        });

        // Place played tokens
        for (let r = 0; r < colState.length; r++) {
            const cell = colDiv.querySelector(`#cell-${actualCol}-${r}`);
            cell.classList.remove('empty');
            
            const token = document.createElement('div');
            token.className = `token ${colState[r]}`;
            
            // Animate only the very last played move if requested
            if (animateLast && c === viewMoves[viewMoves.length - 1] && r === colState.length - 1) {
                token.classList.add('token-drop');
            }
            cell.appendChild(token);
        }

        // Mark the lowest empty cell for the hover preview shadow
        if (colState.length < ROWS) {
            const previewCell = colDiv.querySelector(`#cell-${actualCol}-${colState.length}`);
            if (previewCell) previewCell.classList.add('preview-target');
        }
    }
    
    renderHistory();
    triggerEngine();
}

function handleMove(logicalCol) {
    const col = flipped ? (COLS - 1 - logicalCol) : logicalCol;
    const state = getBoardState(viewMoves);
    if (state[col].length >= ROWS) return; // Column full

    // If we were looking at history and make a move, overwrite future history
    if (viewMoves.length < currentMoves.length) {
        currentMoves = currentMoves.slice(0, viewMoves.length);
    }

    currentMoves.push(col);
    viewMoves = [...currentMoves];
    renderBoard(true);
}

// Update Move History UI
function renderHistory() {
    historyEl.innerHTML = '';
    currentMoves.forEach((col, idx) => {
        const badge = document.createElement('span');
        badge.className = `move-badge ${idx < viewMoves.length ? 'active' : ''}`;
        
        // Display 1-indexed moves (Ply / 2)
        const plyText = (idx % 2 === 0) ? `${(idx/2)+1}. ` : '';
        badge.textContent = `${plyText}${col}`;
        
        badge.onclick = () => {
            viewMoves = currentMoves.slice(0, idx + 1);
            renderBoard(false);
        };
        historyEl.appendChild(badge);
    });
    // Auto-scroll to bottom
    historyEl.scrollTop = historyEl.scrollHeight;
}

// Engine Communication
function triggerEngine() {
    if (!engineEnabled) return;
    document.getElementById('engine-status').textContent = 'Thinking...';
    document.getElementById('engine-status').classList.replace('bg-green-900', 'bg-yellow-900');
    document.getElementById('engine-status').classList.replace('text-green-300', 'text-yellow-300');
    
    engineWorker.postMessage({
        cmd: 'position',
        moves: viewMoves
    });
}

// Receive Data from Web Worker
engineWorker.onmessage = function(e) {
    if (e.data.type === 'search_info') {
        updateAnalyticsPanel(e.data.data);
        updateEvalBar(e.data.data);
    }
};

function updateAnalyticsPanel(info) {
    document.getElementById('stat-depth').textContent = info.depth;
    document.getElementById('stat-nodes').textContent = info.nodes.toLocaleString();
    document.getElementById('stat-nps').textContent = info.nps.toLocaleString();
    
    let scoreText = '';
    if (info.scoreType === 'mate') {
        scoreText = info.scoreValue > 0 ? `M${info.scoreValue}` : `-M${Math.abs(info.scoreValue)}`;
    } else {
        // Convert CP to standard decimal
        scoreText = (info.scoreValue / 100).toFixed(2);
        if (info.scoreValue > 0) scoreText = '+' + scoreText;
    }
    
    // Perspective handling: If P2's turn, negate score for absolute display (optional based on preference)
    const isP1Turn = (viewMoves.length % 2 === 0);
    document.getElementById('stat-score').textContent = scoreText;

    // Render PV
    const pvContainer = document.getElementById('stat-pv');
    pvContainer.innerHTML = '';
    info.pv.forEach(move => {
        const m = document.createElement('span');
        m.className = 'bg-blue-900 text-blue-100 px-1 rounded';
        m.textContent = move;
        pvContainer.appendChild(m);
    });
}

// Evaluation Bar Logic
function updateEvalBar(info) {
    let winChance = 0.5; // 50% neutral

    if (info.scoreType === 'mate') {
        winChance = info.scoreValue > 0 ? 1.0 : 0.0;
    } else {
        // Convert centipawns to win probability via sigmoid function (Standard chess logic)
        // Adjust the scaling divisor (e.g., 400) to tune the sensitivity of the bar
        winChance = 1 / (1 + Math.pow(10, -info.scoreValue / 400));
    }

    // If it's Player 2's turn, negamax scores are from P2's perspective, so invert
    const isP1Turn = (viewMoves.length % 2 === 0);
    const finalWinChance = isP1Turn ? winChance : (1 - winChance);

    // Height of the white bar (P1 is white, P2 is dark space)
    const heightPercentage = Math.max(0, Math.min(100, finalWinChance * 100));
    evalFill.style.height = `${heightPercentage}%`;

    // Format score text
    let displayScore = '';
    if (info.scoreType === 'mate') {
        // Resolve absolute mate
        let absoluteMate = isP1Turn ? info.scoreValue : -info.scoreValue;
        displayScore = absoluteMate > 0 ? `M${absoluteMate}` : `-M${Math.abs(absoluteMate)}`;
    } else {
        let absoluteCP = isP1Turn ? info.scoreValue : -info.scoreValue;
        displayScore = (absoluteCP > 0 ? '+' : '') + (absoluteCP / 100).toFixed(1);
    }
    evalText.textContent = displayScore;
}

// Toolbar Interactions
document.getElementById('btn-flip').onclick = () => {
    flipped = !flipped;
    renderBoard();
};

document.getElementById('btn-undo').onclick = () => {
    if (currentMoves.length > 0) {
        currentMoves.pop();
        viewMoves = [...currentMoves];
        renderBoard();
    }
};

document.getElementById('btn-reset').onclick = () => {
    currentMoves = [];
    viewMoves = [];
    renderBoard();
};

toggleEngineBtn.onchange = (e) => {
    engineEnabled = e.target.checked;
    const status = document.getElementById('engine-status');
    if (!engineEnabled) {
        engineWorker.postMessage({ cmd: 'stop' });
        status.textContent = 'Engine Stopped';
        status.classList.replace('bg-yellow-900', 'bg-gray-700');
        status.classList.replace('text-yellow-300', 'text-gray-300');
    } else {
        triggerEngine();
    }
};

// Bootstrap
initBoard();
