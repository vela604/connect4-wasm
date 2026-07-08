#include <emscripten/bind.h>
#include "uci.h"
#include <string>
#include <iostream>

// Global pointer to persist the engine state in memory
UCI* global_uci = nullptr;

// Initialize the engine once
void init_engine() {
    if (!global_uci) {
        global_uci = new UCI();
        // Send a ready signal to the JS console
        std::cout << "Engine initialized in Wasm!" << std::endl;
    }
}

// Function exposed to JavaScript
void send_uci_command(std::string cmd) {
    if (!global_uci) {
        init_engine();
    }
    // This calls the refactored method in your UCI class
    global_uci->execute_command(cmd);
}

// Emscripten Binding block
EMSCRIPTEN_BINDINGS(engine_module) {
    emscripten::function("initEngine", &init_engine);
    emscripten::function("sendUciCommand", &send_uci_command);
}
