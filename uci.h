#pragma once
#include "engine.h"
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

class UCI {
public:
    UCI();
    ~UCI();

    void run();

private:
    void cmd_uci();
    void cmd_isready();
    void cmd_ucinewgame();
    void cmd_position(const std::string& line);
    void cmd_go(const std::string& line);
    void cmd_stop();
    void cmd_quit();
    void cmd_display();
    void cmd_eval();
    void cmd_perft(int depth);
    void cmd_help();
    void cmd_move(int col);
    void cmd_setoption(const std::string& line);

    bool apply_moves(const std::string& moves_str);
    bool set_fen(const std::string& fen);

    void on_progress(const SearchResult& result, const SearchStats& stats);
    void print_info(const SearchResult& result, const SearchStats& stats);
    void print_final_result(const SearchResult& result);

    uint64_t perft_inner(Position pos, int depth);

    Engine    engine_;
    Position  current_pos_;

    std::thread       search_thread_;
    std::atomic<bool> searching_{false};
    std::atomic<bool> quit_flag_{false};
};

// ───────────────────────────────────────────────────────────────────
//  EMSCRIPTEN GLUE CODE: JS se commands receive karne ke liye
// ───────────────────────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Global UCI instance jo worker use karega
static UCI* g_wasm_uci = nullptr;

extern "C" {
    // Yeh function hamara JS Worker (ccall ke zariye) direct call karega
    EMSCRIPTEN_KEEPALIVE
    void sendUciCommand(const char* cmd_cstr) {
        if (!g_wasm_uci) {
            g_wasm_uci = new UCI();
        }
        
        std::string line(cmd_cstr);
        
        // Trim trailing spaces or carriage returns
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        
        if (line.empty()) return;

        // Route commands directly to UCI class methods
        if (line == "uci")                    g_wasm_uci->cmd_uci();
        else if (line == "isready")                g_wasm_uci->cmd_isready();
        else if (line == "ucinewgame")             g_wasm_uci->cmd_ucinewgame();
        else if (line == "stop")                   g_wasm_uci->cmd_stop();
        else if (line.substr(0, 8) == "position")  g_wasm_uci->cmd_position(line);
        else if (line.substr(0, 2) == "go")        g_wasm_uci->cmd_go(line);
    }
}
#endif
