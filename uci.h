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

    // Original blocking loop (Terminal/CLI ke liye)
    void run();

    // NAYA FUNCTION: WebAssembly (JS) se direct string commands lene ke liye
    // Taaki std::cin par block na hona pade
    void process_command(const std::string& line);

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
