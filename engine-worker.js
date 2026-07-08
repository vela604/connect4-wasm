// 1. ZAROORI STEP: Module pehle define karein taaki Emscripten usko use kar sake
var Module = {
    print: function(text) {
        parseEngineOutput(text);
    },
    printErr: function(text) {
        console.error("Engine Error:", text);
    }
};

// 2. USKE BAAD C++ engine ko import karein
importScripts('connect4_engine.js');

let searchActive = false;

function send_uci_command(cmd) {
    Module.ccall('send_uci_command', 'void', ['string'], [cmd]);
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
        postMessage({ type: 'search_info', data: payload });
    }
}

onmessage = function(e) {
    const msg = e.data;
    if (msg.cmd === 'position') {
        send_uci_command('stop');
        let moveString = msg.moves.join(' ');
        let positionCommand = moveString ? `position startpos moves ${moveString}` : `position startpos`;
        send_uci_command(positionCommand);
        send_uci_command('go infinite');
    } 
    else if (msg.cmd === 'stop') {
        send_uci_command('stop');
    }
};
