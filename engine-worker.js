// engine-worker.js

// 1. Module Configuration
self.Module = {
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

// 2. Load the main Emscripten compiled JS
importScripts('engine.js');

let isSearching = false;
let startTime = 0;

// Helper function to safely send string to WebAssembly via pointer
function sendStringCommand(cmdStr) {
    if (typeof self.Module._sendUciCommand === 'function') {
        // Direct WebAssembly export check
        const length = self.Module.lengthBytesUTF8(cmdStr) + 1;
        const ptr = self.Module._malloc(length);
        self.Module.stringToUTF8(cmdStr, ptr, length);
        self.Module._sendUciCommand(ptr);
        self.Module._free(ptr);
    } else if (typeof _sendUciCommand === 'function') {
        // Fallback context check
        const length = lengthBytesUTF8(cmdStr) + 1;
        const ptr = _malloc(length);
        stringToUTF8(cmdStr, ptr, length);
        _sendUciCommand(ptr);
        _free(ptr);
    } else {
        console.error("Wasm Command Function not found on Module.");
    }
}

// 3. Output Parser (Matches your 577-line uci.cpp formatting structure)
function parseEngineOutput(line) {
    const cleanLine = line.replace(/\x1b\[[0-9;]*m/g, ''); // Clear ANSI

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

        const depthMatch = cleanLine.match(/\[ d\s*(\d+)\s*\]/);
        if (depthMatch) data.depth = parseInt(depthMatch[1], 10);

        const nodesMatch = cleanLine.match(/(\d+)\s*n/);
        if (nodesMatch) {
            data.nodes = parseInt(nodesMatch[1], 10);
            const elapsed = (Date.now() - startTime) / 1000;
            if (elapsed > 0) {
                data.nps = Math.round(data.nodes / elapsed);
            }
        }

        if (cleanLine.includes('pv')) {
            const pvPart = cleanLine.split('pv')[1].trim();
            data.pv = pvPart.split(/\s+/).map(Number).filter(n => !isNaN(n));
        }

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

    if (cleanLine.includes('bestmove')) {
        isSearching = false;
        const parts = cleanLine.trim().split(/\s+/);
        const moveStr = parts[1];
        
        postMessage({
            type: 'bestmove',
            move: moveStr === '(none)' ? null : parseInt(moveStr, 10)
        });
    }
}

// 4. Input Message Listener
self.onmessage = function(e) {
    const msg = e.data;

    if (msg.cmd === 'position') {
        if (isSearching) {
            sendStringCommand('stop');
        }
        
        let uciCmd = 'position startpos';
        if (msg.moves && msg.moves.length > 0) {
            uciCmd += ' moves ' + msg.moves.join(' ');
        }
        
        sendStringCommand(uciCmd);

        startTime = Date.now();
        isSearching = true;
        sendStringCommand('go infinite');
    } 
    else if (msg.cmd === 'stop') {
        if (isSearching) {
            sendStringCommand('stop');
            isSearching = false;
        }
    }
};
