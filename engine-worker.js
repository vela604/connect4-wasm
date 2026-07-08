// engine-worker.js

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

importScripts('engine.js');

let isSearching = false;
let startTime = 0;

// Official Emscripten String Converter Method
function sendStringCommand(cmdStr) {
    const stringToUTF8Fn = self.stringToUTF8 || (self.Module && self.Module.stringToUTF8);
    const lengthBytesUTF8Fn = self.lengthBytesUTF8 || (self.Module && self.Module.lengthBytesUTF8);
    const mallocFn = self._malloc || (self.Module && self.Module._malloc);
    const freeFn = self._free || (self.Module && self.Module._free);
    const sendUciFn = self._sendUciCommand || (self.Module && self.Module._sendUciCommand);

    if (typeof stringToUTF8Fn === 'function' && typeof sendUciFn === 'function') {
        const length = lengthBytesUTF8Fn(cmdStr) + 1;
        const ptr = mallocFn(length);
        stringToUTF8Fn(cmdStr, ptr, length);
        sendUciFn(ptr);
        freeFn(ptr);
    } else {
        console.error("Critical Emscripten methods are missing from environment.");
    }
}

function parseEngineOutput(line) {
    const cleanLine = line.replace(/\x1b\[[0-9;]*m/g, '');

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
            if (elapsed > 0) data.nps = Math.round(data.nodes / elapsed);
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

self.onmessage = function(e) {
    const msg = e.data;
    if (msg.cmd === 'position') {
        if (isSearching) sendStringCommand('stop');
        
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
