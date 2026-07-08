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
        // Analyze starting position
        requestAnalysis();
    } 
    else if (msg.type === 'info' && engineEnabled) {
        updateAnalyticsUI(msg);
    }
};

// ─── INITIALIZATION ──────────────────────────────────────────

function createGrid() {
    boardContainer.innerHTML = '';
    
    for (let c = 0; c < COLS; c++) {
        // Group columns so hovering and clicking is localized to the column
        const colWrapper = document.createElement('div');
        colWrapper.className = 'flex flex-col gap-2 col-hover-group cursor-pointer relative';
        colWrapper.dataset.col = c;
        
        // Ghost piece for hover preview
        const ghost = document.createElement('div');
        ghost.className = 'w-10 h-10 md:w-12 md:h-12 rounded-full absolute -top-12 left-0 opacity-0 transition-opacity duration-200 preview-piece z-20';
        ghost.id = `ghost-col-${c}`;
        colWrapper.appendChild(ghost);

        // Render slots
        for (let r = ROWS - 1; r >= 0; r--) {
            const slot = document.createElement('div');
            slot.className = 'w-10 h-10 md:w-12 md:h-12 rounded-full bg-slate-900 border-[3px] border-blue-800 shadow-inner overflow-hidden relative';
            slot.id = `slot-${c}-${r}`;
            colWrapper.appendChild(slot);
        }

        // Events
        colWrapper.addEventListener('click', () => handleMove(c));
        colWrapper.addEventListener('mouseenter', () => updateGhostColor(c));
        boardContainer.appendChild(colWrapper);
    }
    renderState();
}

function updateGhostColor(col) {
    const ghost = document.getElementById(`ghost-col-${col}`);
    const isPlayer1 = (gameHistory.length % 2 === 0);
    ghost.style.backgroundColor = isPlayer1 ? '#eab308' : '#ef4444'; // Tailwind yellow-500 or red-500
}

// ─── GAME STATE MANAGEMENT ───────────────────────────────────

// Reconstruct 2D array state from move history
function computeBoardState() {
    let state = Array.from({length: COLS}, () => Array(ROWS).fill(0));
    let player = 1;
    for (let move of gameHistory) {
        let row = state[move].indexOf(0);
        if (row !== -1) {
            state[move][row] = player;
            player = player === 1 ? 2 : 1;
        }
    }
    return state;
}

function handleMove(col) {
    const state = computeBoardState();
    if (state[col].indexOf(0) === -1) return; // Column is full

    gameHistory.push(col);
    renderState(true, col); // pass true to trigger drop animation
    updateHistoryUI();
    requestAnalysis();
}

function renderState(animate = false, lastCol = -1) {
    const state = computeBoardState();
    
    for (let c = 0; c < COLS; c++) {
        for (let r = 0; r < ROWS; r++) {
            const slot = document.getElementById(`slot-${c}-${r}`);
            slot.innerHTML = ''; 

            if (state[c][r] !== 0) {
                const token = document.createElement('div');
                token.className = `w-full h-full rounded-full shadow-md ${state[c][r] === 1 ? 'bg-yellow-500' : 'bg-red-500'}`;
                
                // Add drop animation class to the topmost piece in the column just played
                if (animate && c === lastCol) {
                    const topRow = state[c].indexOf(0) === -1 ? ROWS - 1 : state[c].indexOf(0) - 1;
                    if (r === topRow) {
                        token.classList.add('piece-drop');
                    }
                }
                
                slot.appendChild(token);
            }
        }
        updateGhostColor(c); 
    }
}

// ─── ENGINE COMMUNICATION ────────────────────────────────────

function requestAnalysis() {
    if (!engineEnabled) return;
    
    pvContainer.innerHTML = '<span class="text-slate-400 italic">Calculating...</span>';
    worker.postMessage({ cmd: 'position', moves: gameHistory });
}

function updateAnalyticsUI(data) {
    statDepth.textContent = data.depth;
    statNodes.textContent = data.nodes.toLocaleString();
    statNps.textContent = data.nps.toLocaleString();
    evalScore.textContent = data.scoreText;
    
    // Eval Bar Physics (Clamp score and convert to percentage)
    let fill = 50; 
    if (data.mateIn !== null) {
        fill = data.mateIn > 0 ? 100 : 0;
    } else {
        const clamped = Math.max(-1000, Math.min(1000, data.scoreCp));
        fill = 50 + (clamped / 1000) * 50; 
    }
    
    evalFill.style.height = `${fill}%`;
    evalFill.className = `w-full absolute bottom-0 eval-transition ${gameHistory.length % 2 === 1 ? 'bg-slate-900' : 'bg-slate-200'}`;
    
    // Render PV Sequence
    pvContainer.innerHTML = '';
    data.pv.forEach((move) => {
        const span = document.createElement('span');
        span.className = 'px-2 py-0.5 bg-slate-700 rounded cursor-pointer hover:bg-slate-600 transition-colors';
        span.textContent = move;
        span.onclick = () => handleMove(move); // Clicking PV plays the move
        pvContainer.appendChild(span);
    });
}

// ─── RIGHT PANEL UI LOGIC ────────────────────────────────────

function updateHistoryUI() {
    moveHistoryTree.innerHTML = '';
    for (let i = 0; i < gameHistory.length; i += 2) {
        const row = document.createElement('div');
        row.className = 'flex gap-2 items-center hover:bg-slate-700/50 p-1 rounded';
        
        const turnLabel = document.createElement('span');
        turnLabel.className = 'text-slate-500 w-6 text-right';
        turnLabel.textContent = `${(i/2)+1}.`;
        
        const move1 = createHistoryBtn(gameHistory[i], i);
        let move2 = null;
        if (i + 1 < gameHistory.length) {
            move2 = createHistoryBtn(gameHistory[i+1], i + 1);
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
        // Rollback history
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
        evalScore.textContent = 'OFF';
        evalFill.style.height = '50%';
    }
});

// Boot Application
createGrid();
