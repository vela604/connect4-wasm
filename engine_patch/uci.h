#pragma once
#include "engine.h"
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

class UCI {
public:
    // web_mode=true switches print_info()/print_final_result() to emit
    // clean, ANSI-free, single-line "info ..." / "bestmove ..." text
    // instead of the human-oriented colored terminal output. This is
    // what the WebAssembly build (wasm_bridge.cpp) uses so the JS Web
    // Worker can parse engine progress with a simple regex.
    explicit UCI(bool web_mode = false);
    ~UCI();

    void run();

    // Dispatch a single UCI-style command line (same logic run() uses
    // internally for each line read from stdin). Exposed publicly so
    // the Wasm bridge can feed it commands directly via ccall(), one
    // at a time, from JS — without needing an interactive stdin loop.
    void execute(const std::string& line);

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

    bool web_mode_{false};
    std::chrono::steady_clock::time_point go_start_;
};
