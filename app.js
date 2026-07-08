const ROWS = 6;
const COLS = 7;
let currentMoves = [];
let viewMoves = [];
let flipped = false;
let engineEnabled = true;
let engineIsReady = false;

// 1. SETUP EMSCRIPTEN MODULE DIRECTLY IN MAIN THREAD
var Module = {
    print: function(text) {
        parseEngineOutput(text);
    },
    printErr: function(text) {
        console.error("Engine:", text);
    },
    onRuntimeInitialized: function() {
        console.log("WASM Engine Loaded!");
        engineIsReady = true;
        const status = document.getElementById('engine-status');
        status.textContent = 'Engine Ready';
        status.classList.replace('bg-red-900', 'bg-green-900');
        status.classList.replace('text-red-300', 'text-green-300');
    }
};

function send_uci_command(cmd) {
    if (engineIsReady && Module.ccall) {
        Module.ccall('send_uci_command', 'void', ['string'], [cmd]);
    }
}

const INFO_REGEX = /info depth (\d+).*?nodes (\d+).*?score (cp|mate) ([\-\d]+).*?pv ([\d\s]+)/;

function parseEngineOutput(line) {
    const match = line.match(INFO_REGEX);
    if (match) {
        const payload = {
            depth: parseInt(match[1], 10),
            nodes: parseInt(match[2], 10),
            nps: 0,
            scoreType: match[3],
            scoreValue: parseInt(match[4], 10),
            pv: match[5].trim().split(' ').map(Number)
        };
        updateAnalyticsPanel(payload);
        updateEvalBar(payload);
    }
}

// DOM Elements
const gridEl = document.getElementById('grid');
const evalFill = document.getElementById('eval-fill');
const evalText = document.getElementById('eval-text');
const historyEl = document.getElementById('move-history');
const toggleEngineBtn = document.getElementById('toggle-engine');

function initBoard() {
    gridEl.innerHTML = '';
    for (let c = 0; c < COLS; c++) {
        const colDiv = document.createElement('div');
        colDiv.className = 'col-interactive flex flex-col gap-2 py-1';
        colDiv.dataset.col = c;
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

function getBoardState(moves) {
    let state = Array.from({ length: COLS }, () => []);
    moves.forEach((col, idx) => {
        const player = (idx % 2 === 0) ? 'p1' : 'p2';
        state[col].push(player);
    });
    return state;
}

function renderBoard(animateLast = false) {
    const state = getBoardState(viewMoves);
    const currentPlayer = (viewMoves.length % 2 === 0) ? 'hover-p1' : 'hover-p2';

    document.querySelectorAll('.col-interactive').forEach(col => {
        col.classList.remove('hover-p1', 'hover-p2');
        col.classList.add(currentPlayer);
    });

    for (let c = 0; c < COLS; c++) {
        const colState = state[c];
        const actualCol = flipped ? (COLS - 1 - c) : c;
        const colDiv = document.querySelector(`.col-interactive[data-col="${actualCol}"]`);
        
        colDiv.querySelectorAll('.c4-cell').forEach(cell => {
            cell.innerHTML = '';
            cell.classList.add('empty');
            cell.classList.remove('preview-target');
        });

        for (let r = 0; r < colState.length; r++) {
            const cell = colDiv.querySelector(`#cell-${actualCol}-${r}`);
            cell.classList.remove('empty');
            const token = document.createElement('div');
            token.className = `token ${colState[r]}`;
            if (animateLast && c === viewMoves[viewMoves.length - 1] && r === colState.length - 1) {
                token.classList.add('token-drop');
            }
            cell.appendChild(token);
        }

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
    if (state[col].length >= ROWS) return; 

    if (viewMoves.length < currentMoves.length) {
        currentMoves = currentMoves.slice(0, viewMoves.length);
    }

    currentMoves.push(col);
    viewMoves = [...currentMoves];
    renderBoard(true);
}

function renderHistory() {
    historyEl.innerHTML = '';
    currentMoves.forEach((col, idx) => {
        const badge = document.createElement('span');
        badge.className = `move-badge ${idx < viewMoves.length ? 'active' : ''}`;
        const plyText = (idx % 2 === 0) ? `${(idx/2)+1}. ` : '';
        badge.textContent = `${plyText}${col}`;
        badge.onclick = () => {
            viewMoves = currentMoves.slice(0, idx + 1);
            renderBoard(false);
        };
        historyEl.appendChild(badge);
    });
    historyEl.scrollTop = historyEl.scrollHeight;
}

function triggerEngine() {
    if (!engineEnabled || !engineIsReady) return;
    const status = document.getElementById('engine-status');
    status.textContent = 'Thinking...';
    status.classList.replace('bg-green-900', 'bg-yellow-900');
    status.classList.replace('text-green-300', 'text-yellow-300');
    
    send_uci_command('stop');
    let moveString = viewMoves.join(' ');
    let positionCommand = moveString ? `position startpos moves ${moveString}` : `position startpos`;
    send_uci_command(positionCommand);
    send_uci_command('go infinite');
}

function updateAnalyticsPanel(info) {
    document.getElementById('stat-depth').textContent = info.depth;
    document.getElementById('stat-nodes').textContent = info.nodes.toLocaleString();
    
    let scoreText = '';
    const isP1Turn = (viewMoves.length % 2 === 0);
    if (info.scoreType === 'mate') {
        let absoluteMate = isP1Turn ? info.scoreValue : -info.scoreValue;
        scoreText = absoluteMate > 0 ? `M${absoluteMate}` : `-M${Math.abs(absoluteMate)}`;
    } else {
        let absoluteCP = isP1Turn ? info.scoreValue : -info.scoreValue;
        scoreText = (absoluteCP > 0 ? '+' : '') + (absoluteCP / 100).toFixed(2);
    }
    document.getElementById('stat-score').textContent = scoreText;

    const pvContainer = document.getElementById('stat-pv');
    pvContainer.innerHTML = '';
    info.pv.forEach(move => {
        const m = document.createElement('span');
        m.className = 'bg-blue-900 text-blue-100 px-1 rounded';
        m.textContent = move;
        pvContainer.appendChild(m);
    });
}

function updateEvalBar(info) {
    let winChance = 0.5; 
    if (info.scoreType === 'mate') {
        winChance = info.scoreValue > 0 ? 1.0 : 0.0;
    } else {
        winChance = 1 / (1 + Math.pow(10, -info.scoreValue / 400));
    }

    const isP1Turn = (viewMoves.length % 2 === 0);
    const finalWinChance = isP1Turn ? winChance : (1 - winChance);

    const heightPercentage = Math.max(0, Math.min(100, finalWinChance * 100));
    evalFill.style.height = `${heightPercentage}%`;

    let displayScore = '';
    if (info.scoreType === 'mate') {
        let absoluteMate = isP1Turn ? info.scoreValue : -info.scoreValue;
        displayScore = absoluteMate > 0 ? `M${absoluteMate}` : `-M${Math.abs(absoluteMate)}`;
    } else {
        let absoluteCP = isP1Turn ? info.scoreValue : -info.scoreValue;
        displayScore = (absoluteCP > 0 ? '+' : '') + (absoluteCP / 100).toFixed(1);
    }
    evalText.textContent = displayScore;
}

document.getElementById('btn-flip').onclick = () => { flipped = !flipped; renderBoard(); };
document.getElementById('btn-undo').onclick = () => {
    if (currentMoves.length > 0) { currentMoves.pop(); viewMoves = [...currentMoves]; renderBoard(); }
};
document.getElementById('btn-reset').onclick = () => { currentMoves = []; viewMoves = []; renderBoard(); };
toggleEngineBtn.onchange = (e) => {
    engineEnabled = e.target.checked;
    const status = document.getElementById('engine-status');
    if (!engineEnabled) {
        send_uci_command('stop');
        status.textContent = 'Engine Stopped';
        status.className = "px-3 py-1 rounded-full bg-gray-700 text-gray-300 text-xs font-bold uppercase tracking-wide";
    } else {
        status.className = "px-3 py-1 rounded-full bg-green-900 text-green-300 text-xs font-bold uppercase tracking-wide";
        triggerEngine();
    }
};

initBoard();
