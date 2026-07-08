#include "uci.h"
#include <iostream>
#include <csignal>
#include <cstdlib>

// Global UCI instance for signal handling (used by SIGINT)
static UCI* g_uci = nullptr;

// FIX: was incorrectly calling uci.run() again on signal.
// Now just prints a message and exits cleanly.
void signal_handler(int /*sig*/) {
    std::cerr << "\n  [Ctrl+C received — exiting]\n";
    std::exit(0);
}

int main(int /*argc*/, char* /*argv*/[]) {
    // Register SIGINT so Ctrl+C gives a clean exit message
    signal(SIGINT, signal_handler);

    UCI uci;
    g_uci = &uci;
    uci.run();

    return 0;
}

// main.cpp ke end mein add karein
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C" {
    // Ye function JavaScript call karega
    EMSCRIPTEN_KEEPALIVE
    void send_uci_command(const char* command) {
        if (g_uci) {
            // Aapko apne uci.h me ek public function banana padega `process_command(std::string cmd)`
            // jo lines ko waise hi handle kare jaise while(getline) wala loop karta tha.
            g_uci->process_command(std::string(command)); 
        }
    }
}
#endif

