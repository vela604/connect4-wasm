// engine-worker.js

// 1. Emscripten Module Setup
var Module = {
    // Intercept standard output (std::cout) from C++
    print: function(text) {
        parseEngineOutput(text);
    },
    printErr: function(text) {
        console.error("Engine Error:", text);
    },
    onRuntimeInitialized: function() {
        // Tell the main thread the engine is ready
        postMessage({ type: 'ready' });
    }
};

// Load the Emscripten-compiled JS glue code
// Make sure your compiled Wasm JS file is named exactly 'engine.js'
importScripts('engine.js');

let isSearching = false;
let startTime = 0;

// 2. Output Parser
function parseEngineOutput(line) {
    // Remove ANSI color codes that your C++ engine outputs
    const cleanLine = line.replace(/\x1b\[[0-9;]*m/g, '');

    // Look for lines that indicate search progress
    // Example: "[ d 8 ]  +WIN in 3  mv:3 ... 14205n ... pv 3 4 2"
    if (cleanLine.includes('[ d')) {
        const data = {
            type: 'info',
            depth: 0,
            scoreText: '0.0',
            scoreCp: 0, // Centipawn-equivalent score for the eval bar
            mateIn: null,
            nodes: 0,
            nps: 0,
            pv: []
        };

        // Extract Depth
        const depthMatch = cleanLine.match(/\[ d\s*(\d+) \]/);
        if (depthMatch) data.depth = parseInt(depthMatch[1], 10);

        // Extract Nodes & Calculate NPS
        const nodeMatch = cleanLine.match(/(\d+)n\s/);
        if (nodeMatch) {
            data.nodes = parseInt(nodeMatch[1], 10);
            const elapsedSecs = (Date.now() - startTime) / 1000;
            if (elapsedSecs > 0) {
                data.nps = Math.floor(data.nodes / elapsedSecs);
            }
        }

        // Extract PV (Principal Variation)
        const pvMatch = cleanLine.match(/pv\s+([\d\s]+)/);
        if (pvMatch) {
            data.pv = pvMatch[1].trim().split(/\s+/).map(Number);
        }

        // Extract Score
        if (cleanLine.includes('WIN in')) {
            const mateMatch = cleanLine.match(/WIN in (\d+)/);
            if (mateMatch) {
                data.mateIn = parseInt(mateMatch[1], 10);
                data.scoreText = `+M${data.mateIn}`;
                data.scoreCp = 10000; // Arbitrary high positive value
            }
        } else if (cleanLine.includes('LOSS in')) {
            const mateMatch = cleanLine.match(/LOSS in (\d+)/);
            if (mateMatch) {
                data.mateIn = -parseInt(mateMatch[1], 10);
                data.scoreText = `-M${-data.mateIn}`;
                data.scoreCp = -10000; // Arbitrary high negative value
            }
        } else if (cleanLine.includes('=  DRAW')) {
            data.scoreText = '0.0';
            data.scoreCp = 0;
        } else {
            // It's a standard numerical score (e.g., +42)
            const cpMatch = cleanLine.match(/([+-]\s*\d+)/);
            if (cpMatch && !cpMatch[0].includes('WIN') && !cpMatch[0].includes('LOSS')) {
                const scoreStr = cpMatch[1].replace(/\s+/g, '');
                data.scoreCp = parseInt(scoreStr, 10);
                // Format like chess.com (+0.42) if your engine scores in small integers
                data.scoreText = (data.scoreCp > 0 ? '+' : '') + (data.scoreCp / 100).toFixed(1);
            }
        }

        // Send parsed data back to UI
        postMessage(data);
    }
}

// 3. Command Listener
onmessage = function(e) {
    const msg = e.data;

    if (msg.cmd === 'position') {
        // Halt any ongoing search
        if (isSearching) {
            Module.ccall('sendUciCommand', 'void', ['string'], ['stop']);
        }
        
        // Construct the position command based on your UCI class logic
        let uciCmd = 'position startpos';
        if (msg.moves && msg.moves.length > 0) {
            uciCmd += ' moves ' + msg.moves.join(' ');
        }
        
        // Send position command
        Module.ccall('sendUciCommand', 'void', ['string'], [uciCmd]);

        // Immediately start searching infinitely
        startTime = Date.now();
        isSearching = true;
        Module.ccall('sendUciCommand', 'void', ['string'], ['go infinite']);
    } 
    else if (msg.cmd === 'stop') {
        Module.ccall('sendUciCommand', 'void', ['string'], ['stop']);
        isSearching = false;
    }
};
