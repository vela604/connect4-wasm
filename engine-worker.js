// engine-worker.js

// 1. Emscripten Module Setup
var Module = {
    print: function(text) {
        parseEngineOutput(text);
    },
    printErr: function(text) {
        console.error("Engine Error:", text);
    },
    onRuntimeInitialized: function() {
        postMessage({ type: 'ready' });
    }
};

importScripts('engine.js');

let isSearching = false;
let startTime = 0;

// 2. Output Parser (Strictly matched with your uci.cpp layout)
function parseEngineOutput(line) {
    const cleanLine = line.replace(/\x1b\[[0-9;]*m/g, ''); // Remove ANSI Colors

    // Check for search progress line: "[ d 8 ]"
    if (cleanLine.includes('[ d')) {
        const data = {
            type: 'info',
            depth: 0,
            scoreText: '0.0',
            scoreCp: 0,
            mateIn: null,
            nodes: 0,
            nps: 0,
            pv: []
        };

        // 1. Extract Depth
        const depthMatch = cleanLine.match(/\[ d\s*(\d+)\s*\]/);
        if (depthMatch) data.depth = parseInt(depthMatch[1], 10);

        // 2. Extract Best Move for this depth (mv:X)
        const mvMatch = cleanLine.match(/mv:\s*(\d+)/);
        if (mvMatch) {
            // Optional: can log it if needed
        }

        // 3. Extract Nodes (e.g., "    14205n")
        const nodesMatch = cleanLine.match(/(\d+)\s*n/);
        if (nodesMatch) {
            data.nodes = parseInt(nodesMatch[1], 10);
            const elapsed = (Date.now() - startTime) / 1000;
            if (elapsed > 0) {
                data.nps = Math.round(data.nodes / elapsed);
            }
        }

        // 4. Extract PV Line (e.g., "pv 3 4 2")
        if (cleanLine.includes('pv')) {
            const pvPart = cleanLine.split('pv')[1].trim();
            data.pv = pvPart.split(/\s+/).map(Number).filter(n => !isNaN(n));
        }

        // 5. Extract Score (+WIN, -LOSS, or Numeric Eval)
        if (cleanLine.includes('WIN')) {
            const mateMatch = cleanLine.match(/in\s*(\d+)/) || cleanLine.match(/WIN\/(\d+)/);
            data.mateIn = mateMatch ? parseInt(mateMatch[1], 10) : 1;
            data.scoreText = `Mate in ${data.mateIn}`;
            data.scoreCp = 10000; 
        } else if (cleanLine.includes('LOSS')) {
            const mateMatch = cleanLine.match(/in\s*(\d+)/) || cleanLine.match(/LOSS\/(\d+)/);
            data.mateIn = mateMatch ? -parseInt(mateMatch[1], 10) : -1;
            data.scoreText = `Mated in ${Math.abs(data.mateIn)}`;
            data.scoreCp = -10000;
        } else if (cleanLine.includes('=  DRAW')) {
            data.scoreText = '0.0';
            data.scoreCp = 0;
        } else {
            // Standard score evaluation (+ or - values)
            const scoreMatch = cleanLine.match(/([+-])\s*(\d+)/);
            if (scoreMatch) {
                const sign = scoreMatch[1];
                const value = parseInt(scoreMatch[2], 10);
                data.scoreCp = sign === '-' ? -value : value;
                data.scoreText = (data.scoreCp / 100).toFixed(2);
            }
        }

        postMessage(data);
    }

    // Check for final decision command: "bestmove X" or "bestmove (none)"
    if (cleanLine.includes('bestmove')) {
        isSearching = false;
        const parts = cleanLine.trim().split(/\s+/);
        const moveStr = parts[1]; // can be a number 0-6 or "(none)"
        
        postMessage({
            type: 'bestmove',
            move: moveStr === '(none)' ? null : parseInt(moveStr, 10)
        });
    }
}

// 3. Command Listener
onmessage = function(e) {
    const msg = e.data;

    if (msg.cmd === 'position') {
        if (isSearching) {
            Module.ccall('sendUciCommand', 'void', ['string'], ['stop']);
        }
        
        let uciCmd = 'position startpos';
        if (msg.moves && msg.moves.length > 0) {
            uciCmd += ' moves ' + msg.moves.join(' ');
        }
        
        Module.ccall('sendUciCommand', 'void', ['string'], [uciCmd]);

        startTime = Date.now();
        isSearching = true;
        Module.ccall('sendUciCommand', 'void', ['string'], ['go infinite']);
    } 
    else if (msg.cmd === 'stop') {
        if (isSearching) {
            Module.ccall('sendUciCommand', 'void', ['string'], ['stop']);
            isSearching = false;
        }
    }
};
