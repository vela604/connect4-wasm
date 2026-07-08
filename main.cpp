#include "uci.h"
#include <iostream>
#include <csignal>
#include <cstdlib>

// Global UCI instance
static UCI* g_uci = nullptr;

void signal_handler(int /*sig*/) {
    std::cerr << "\n  [Ctrl+C received — exiting]\n";
    std::exit(0);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C" {
    // JavaScript direct is function ko call karega
    EMSCRIPTEN_KEEPALIVE
    void send_uci_command(const char* command) {
        if (g_uci) {
            g_uci->process_command(std::string(command)); 
        }
    }
}
#endif

int main(int /*argc*/, char* /*argv*/[]) {
    signal(SIGINT, signal_handler);

#ifdef __EMSCRIPTEN__
    // WebAssembly Mode: 
    // Isko heap (new) par allocate karenge taaki main() return hone ke baad 
    // bhi engine memory me zinda rahe aur JS commands receive kar sake.
    g_uci = new UCI();
    std::cerr << "Wasm Engine Initialized & Waiting for commands...\n";
    // YAHAN uci.run() CALL NAHI KARNA HAI. Main function silently exit ho jayega.
#else
    // Native CLI Mode (PC/Mac/Linux Terminal ke liye)
    UCI uci;
    g_uci = &uci;
    uci.run(); // Ye apna normal blocking loop chalayega
#endif

    return 0;
}
