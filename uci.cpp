#include "uci.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio> // printf aur fflush ke liye zaroori

// CLI aesthetics (sirf display() aur terminal mode ke liye)
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_WHITE   "\033[37m"
#define ANSI_GRAY    "\033[90m"

UCI::UCI() : engine_(256) {
    engine_.set_progress_callback(
        [this](const SearchResult& r, const SearchStats& s) {
            on_progress(r, s);
        }
    );
}

UCI::~UCI() {
    engine_.stop();
    if (search_thread_.joinable()) search_thread_.join();
}

static void print_board(const Position& pos) {
    BB p1, p2;
    if (pos.side_to_move() == 0) { p1 = pos.current; p2 = pos.opponent(); }
    else                          { p2 = pos.current; p1 = pos.opponent(); }

    std::cerr << "\n" << ANSI_BOLD << "  ╔═══════════════╗\n" << ANSI_RESET;
    for (int r = ROWS - 1; r >= 0; --r) {
        std::cerr << ANSI_BOLD << "  ║ " << ANSI_RESET;
        for (int c = 0; c < COLS; ++c) {
            BB bit = BB(1) << (c + r * COLS);
            if      (p1 & bit) std::cerr << ANSI_YELLOW << ANSI_BOLD << "● " << ANSI_RESET;
            else if (p2 & bit) std::cerr << ANSI_RED    << ANSI_BOLD << "● " << ANSI_RESET;
            else               std::cerr << ANSI_GRAY   << "· "              << ANSI_RESET;
        }
        std::cerr << ANSI_BOLD << "║" << ANSI_RESET << "\n";
    }
    std::cerr << ANSI_BOLD << "  ╚═══════════════╝\n" << ANSI_RESET;
    std::cerr << ANSI_CYAN  << "    0 1 2 3 4 5 6\n"  << ANSI_RESET << "\n";

    std::string mover = (pos.side_to_move() == 0)
        ? std::string(ANSI_YELLOW) + "P1 (Yellow ●)" + ANSI_RESET
        : std::string(ANSI_RED)    + "P2 (Red ●)"    + ANSI_RESET;
    std::cerr << "  Move " << ANSI_BOLD << pos.moves << ANSI_RESET
              << "  |  " << mover << " to play\n\n";
    std::cerr.flush();
}

void UCI::on_progress(const SearchResult& r, const SearchStats& s) {
    print_info(r, s);
}

// ─────────────────────────────────────────────
//  WASM-SAFE PRINTERS (Uses printf + fflush)
// ─────────────────────────────────────────────
void UCI::print_info(const SearchResult& result, const SearchStats& stats) {
    uint64_t nodes = stats.nodes_explored.load();
    
    printf("info depth %d nodes %llu nps 0 score ", result.depth, (unsigned long long)nodes);

    if (std::abs(result.score) >= MATE_THRESHOLD) {
        int mate_val = (result.score > 0) ? result.mate_in : -result.mate_in;
        printf("mate %d", mate_val);
    } else {
        printf("cp %d", result.score);
    }

    if (!result.pv.empty()) {
        printf(" pv");
        for (int c : result.pv) printf(" %d", c);
    }

    printf("\n");
    fflush(stdout); // Forces Wasm to send data to JS immediately!
}

void UCI::print_final_result(const SearchResult& result) {
    if (result.depth == 0 && result.best_move < 0) return; 

    if (result.best_move >= 0) {
        printf("bestmove %d\n", result.best_move);
    } else {
        printf("bestmove none\n");
    }
    fflush(stdout); 
}

void UCI::cmd_uci() {
    printf("id name Connect4-Engine v1.0\n");
    printf("id author Connect4-Engine\n");
    printf("option name Hash type spin default 256 min 1 max 4096\n");
    printf("option name Threads type spin default 0 min 0 max 256\n");
    printf("uciok\n");
    fflush(stdout);
}

void UCI::cmd_isready() {
    printf("readyok\n");
    fflush(stdout);
}

void UCI::cmd_ucinewgame() {
    engine_.reset();
    current_pos_ = Position{};
    std::cerr << "  [New game]\n";
    std::cerr.flush();
}

void UCI::cmd_display() { print_board(current_pos_); }

void UCI::cmd_eval() {
    int score = evaluate(current_pos_);
    std::cerr << "  Static eval: " << score << "\n";
    int ct = count_threats(current_pos_.current,  current_pos_.opponent());
    int ot = count_threats(current_pos_.opponent(), current_pos_.current);
    std::cerr << "  My threats: " << ct << "  Opp threats: " << ot << "\n";
    std::cerr.flush();
}

void UCI::cmd_help() {
    std::cerr << ANSI_BOLD
              << "\n  ╔══════════════════════════════════╗\n"
              << "  ║   Connect-4 Engine — Commands    ║\n"
              << "  ╠══════════════════════════════════╣\n"
              << "  ║  uci / isready / ucinewgame      ║\n"
              << "  ║  display (d)    show board       ║\n"
              << "  ║  eval (e)       static eval      ║\n"
              << "  ║  position startpos [moves ...]   ║\n"
              << "  ║  go depth <N>   fixed depth      ║\n"
              << "  ║  go infinite    until stop/enter ║\n"
              << "  ║  stop           stop search      ║\n"
              << "  ║  move <col>     play 0-6         ║\n"
              << "  ║  perft <depth>  node count       ║\n"
              << "  ║  setoption name Threads value N  ║\n"
              << "  ║  quit (q)       exit             ║\n"
              << "  ╚══════════════════════════════════╝\n\n"
              << ANSI_RESET;
    std::cerr.flush();
}

void UCI::cmd_setoption(const std::string& line) {
    size_t np = line.find("name ");
    size_t vp = line.find(" value ");
    if (np == std::string::npos) return;

    std::string name = (vp != std::string::npos)
        ? line.substr(np + 5, vp - (np + 5))
        : line.substr(np + 5);
    while (!name.empty() && name.back() == ' ')  name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());

    std::string value = (vp != std::string::npos) ? line.substr(vp + 7) : "";

    if (name == "Threads") {
        int n = 0;
        try { n = std::stoi(value); } catch (...) { n = 0; }
        if (n < 0) n = 0;
        engine_.set_threads(static_cast<unsigned>(n));
    }
}

// BUG 2 FIX: Ab ye function spaced (3 4) aur continuous (34) dono moves ko flawlessly handle karega
bool UCI::apply_moves(const std::string& moves_str) {
    std::istringstream iss(moves_str);
    std::string token;
    while (iss >> token) {
        for (char c : token) {
            if (c >= '0' && c <= '6') {
                int col = c - '0';
                if (!current_pos_.can_play(col)) return false;
                current_pos_ = current_pos_.after(col);
            }
        }
    }
    return true;
}

void UCI::cmd_position(const std::string& line) {
    size_t sp = line.find("startpos");
    size_t fp = line.find("fen ");
    size_t mp = line.find(" moves ");

    if (sp != std::string::npos) {
        current_pos_ = Position{};
    } else if (fp != std::string::npos) {
        std::string fs;
        size_t fstart = fp + 4;
        fs = (mp != std::string::npos) ? line.substr(fstart, mp - fstart)
                                       : line.substr(fstart);
        if (!set_fen(fs)) return;
    }
    
    if (mp != std::string::npos) {
        apply_moves(line.substr(mp + 7));
    }
}

bool UCI::set_fen(const std::string& fen) {
    current_pos_ = Position{};
    std::istringstream iss(fen);
    std::string board_str;
    if (!(iss >> board_str)) return false;
    
    int row = ROWS - 1, col = 0;
    int moves_played = 0;
    
    for (char ch : board_str) {
        if (ch == '/') { --row; col = 0; if (row < 0) break; }
        else if (ch == '.' || ch == '-') { ++col; }
        else if (ch == 'X' || ch == 'x' || ch == '1') {
            if (row >= 0 && col < COLS) {
                current_pos_.mask |= BB(1) << (col + row * COLS);
                moves_played++;
            }
            ++col;
        } else if (ch == 'O' || ch == 'o' || ch == '2') {
            if (row >= 0 && col < COLS) {
                current_pos_.mask |= BB(1) << (col + row * COLS);
                moves_played++;
            }
            ++col;
        }
    }
    current_pos_.moves = moves_played;
    return true;
}

void UCI::cmd_go(const std::string& line) {
    if (searching_) {
        engine_.stop();
    }
    if (search_thread_.joinable()) {
        search_thread_.join();
    }
    searching_ = false;

    int fixed_depth = 0;
    size_t dp = line.find("depth ");
    if (dp != std::string::npos) {
        std::istringstream iss(line.substr(dp + 6));
        iss >> fixed_depth;
    }

    bool unlimited = (line.find("infinite") != std::string::npos || fixed_depth == 0);

    Position pos_copy = current_pos_;
    searching_ = true;

    engine_.reset();
    current_pos_ = pos_copy;

    search_thread_ = std::thread([this, pos_copy, fixed_depth]() {
        SearchResult result = engine_.search(pos_copy, fixed_depth);
        print_final_result(result);
        searching_ = false;
    });

    if (!unlimited) {
        search_thread_.join();
    }
}

void UCI::cmd_move(int col) {
    if (col < 0 || col >= COLS || !current_pos_.can_play(col)) return;
    if (is_win(current_pos_.current) || is_win(current_pos_.opponent())) return;
    current_pos_ = current_pos_.after(col);
}

void UCI::cmd_stop() {
    if (!searching_) return;
    engine_.stop();
    if (search_thread_.joinable()) search_thread_.join();
    searching_ = false;
}

void UCI::cmd_quit() {
    if (searching_) {
        engine_.stop();
    }
    if (search_thread_.joinable()) {
        search_thread_.join();
    }
    searching_ = false;
    quit_flag_ = true;
}

uint64_t UCI::perft_inner(Position pos, int depth) {
    if (depth == 0) return 1;
    if (is_win(pos.current) || is_win(pos.opponent())) return 0;
    if (pos.is_full()) return 0;
    uint64_t count = 0;
    for (int c = 0; c < COLS; ++c)
        if (pos.can_play(c))
            count += perft_inner(pos.after(c), depth - 1);
    return count;
}

void UCI::cmd_perft(int depth) {
    auto t0 = std::chrono::high_resolution_clock::now();
    uint64_t n = perft_inner(current_pos_, depth);
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t0).count();
    std::cerr << "  Nodes: " << n
              << "  Time: " << std::fixed << std::setprecision(1) << ms << "ms\n";
    std::cerr.flush();
}

// ─────────────────────────────────────────────
// BUG 1 FIX: COMMAND PROCESSOR (Crash-Proof Version)
// .find(cmd) == 0 ka use kiya gaya hai taaki pure string match hone tak 
// koi out_of_range error throw na ho.
// ─────────────────────────────────────────────
void UCI::process_command(const std::string& raw_line) {
    std::string line = raw_line;
    
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();

    if (line.empty()) {
        if (searching_) cmd_stop();
        return;
    }

    if      (line == "uci")                    cmd_uci();
    else if (line == "isready")                cmd_isready();
    else if (line == "ucinewgame")             cmd_ucinewgame();
    else if (line == "display" || line == "d") cmd_display();
    else if (line == "eval"    || line == "e") cmd_eval();
    else if (line == "stop")                   cmd_stop();
    else if (line == "quit" || line == "exit" || line == "q") cmd_quit();
    else if (line == "help" || line == "h")    cmd_help();
    
    // SAFE PREFIX MATCHING
    else if (line.find("position") == 0)       cmd_position(line);
    else if (line.find("setoption") == 0)      cmd_setoption(line);
    else if (line.find("go") == 0)             cmd_go(line);
    else if (line.find("perft") == 0) {
        std::istringstream iss(line.substr(5));
        int d = 5; iss >> d; cmd_perft(d);
    }
    else if (line.find("move") == 0) {
        std::istringstream iss(line.substr(4));
        int col = -1; iss >> col; cmd_move(col);
    }
    else {
        std::cerr << "  Unknown command: '" << line << "'\n";
    }
}

// ─────────────────────────────────────────────
//  Main CLI loop (Native build ke liye)
// ─────────────────────────────────────────────
void UCI::run() {
    std::cerr << ANSI_BOLD << ANSI_CYAN
              << "\n  Connect-4 Engine v1.0\n"
              << "  Negamax + Alpha-Beta + TT (Lazy SMP)\n"
              << ANSI_RESET
              << "  Detected " << ANSI_BOLD << Engine::hardware_threads() << ANSI_RESET
              << " CPU thread(s)\n\n";
    std::cerr.flush();

    std::string line;
    while (!quit_flag_) {
        if (!searching_) {
            std::cerr << ANSI_GRAY << "> " << ANSI_RESET;
            std::cerr.flush();
        }
        if (!std::getline(std::cin, line)) break;
        process_command(line);
    }
}
