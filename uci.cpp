#include "uci.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>

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

// ─────────────────────────────────────────────
//  Board printer
// ─────────────────────────────────────────────
static void print_board(const Position& pos) {
    BB p1, p2;
    if (pos.side_to_move() == 0) { p1 = pos.current; p2 = pos.opponent(); }
    else                          { p2 = pos.current; p1 = pos.opponent(); }

    std::cout << "\n" << ANSI_BOLD << "  ╔═══════════════╗\n" << ANSI_RESET;
    for (int r = ROWS - 1; r >= 0; --r) {
        std::cout << ANSI_BOLD << "  ║ " << ANSI_RESET;
        for (int c = 0; c < COLS; ++c) {
            BB bit = BB(1) << (c + r * COLS);
            if      (p1 & bit) std::cout << ANSI_YELLOW << ANSI_BOLD << "● " << ANSI_RESET;
            else if (p2 & bit) std::cout << ANSI_RED    << ANSI_BOLD << "● " << ANSI_RESET;
            else               std::cout << ANSI_GRAY   << "· "              << ANSI_RESET;
        }
        std::cout << ANSI_BOLD << "║" << ANSI_RESET << "\n";
    }
    std::cout << ANSI_BOLD << "  ╚═══════════════╝\n" << ANSI_RESET;
    std::cout << ANSI_CYAN  << "    0 1 2 3 4 5 6\n"  << ANSI_RESET << "\n";

    std::string mover = (pos.side_to_move() == 0)
        ? std::string(ANSI_YELLOW) + "P1 (Yellow ●)" + ANSI_RESET
        : std::string(ANSI_RED)    + "P2 (Red ●)"    + ANSI_RESET;
    std::cout << "  Move " << ANSI_BOLD << pos.moves << ANSI_RESET
              << "  |  " << mover << " to play\n\n";
}

// ─────────────────────────────────────────────
//  Score formatter
// ─────────────────────────────────────────────
static std::string format_score(int score, int mate_in) {
    std::ostringstream oss;
    if (std::abs(score) >= MATE_THRESHOLD) {
        if (score > 0) {
            oss << ANSI_GREEN << ANSI_BOLD << "+WIN";
            if (mate_in > 0) oss << " in " << mate_in;
            oss << ANSI_RESET;
        } else {
            oss << ANSI_RED << ANSI_BOLD << "-LOSS";
            if (mate_in < 0) oss << " in " << -mate_in;
            oss << ANSI_RESET;
        }
    } else if (score == 0) {
        oss << ANSI_WHITE << "=  DRAW" << ANSI_RESET;
    } else if (score > 0) {
        oss << ANSI_GREEN << "+" << std::setw(6) << score << ANSI_RESET;
    } else {
        oss << ANSI_RED   << std::setw(7) << score << ANSI_RESET;
    }
    return oss.str();
}

// ─────────────────────────────────────────────
//  Progress callback — called after each depth
//  Sirf ek line print karta hai, koi live thread nahi
// ─────────────────────────────────────────────
void UCI::on_progress(const SearchResult& r, const SearchStats& s) {
    print_info(r, s);
}

void UCI::print_info(const SearchResult& result, const SearchStats& stats) {
    uint64_t nodes   = stats.nodes_explored.load();
    uint64_t pruned  = stats.nodes_pruned.load();
    uint64_t tt_hits = stats.tt_hits.load();
    double   pct_p   = (nodes + pruned) > 0
                       ? 100.0 * pruned / (nodes + pruned) : 0.0;

    std::cout << ANSI_BOLD << ANSI_CYAN
              << "[ d" << std::setw(2) << result.depth << " ] "
              << ANSI_RESET
              << format_score(result.score, result.mate_in) << "  ";

    if (result.best_move >= 0)
        std::cout << "mv:" << ANSI_MAGENTA << ANSI_BOLD
                  << result.best_move << ANSI_RESET << "  ";

    std::cout << ANSI_WHITE << std::setw(9) << nodes << ANSI_RESET << "n  "
              << ANSI_YELLOW
              << std::fixed << std::setprecision(1) << pct_p << "%"
              << ANSI_RESET << "prn  "
              << "tt:" << tt_hits;

    if (!result.pv.empty()) {
        std::cout << "  pv";
        for (int c : result.pv) std::cout << " " << c;
    }

    if (std::abs(result.score) >= MATE_THRESHOLD) {
        if (result.score > 0)
            std::cout << "  " << ANSI_GREEN << ANSI_BOLD
                      << "★WIN/" << result.mate_in << ANSI_RESET;
        else
            std::cout << "  " << ANSI_RED << ANSI_BOLD
                      << "✗LOSS/" << -result.mate_in << ANSI_RESET;
    }

    std::cout << "\n";
    std::cout.flush();
}

// ─────────────────────────────────────────────
//  Commands
// ─────────────────────────────────────────────
void UCI::cmd_uci() {
    std::cout << "id name Connect4-Engine v1.0\n"
              << "id author Connect4-Engine\n"
              << "option name Hash type spin default 256 min 1 max 4096\n"
              << "option name Threads type spin default 0 min 0 max 256\n"
              << "uciok\n";
    std::cout.flush();
}

void UCI::cmd_isready() {
    std::cout << "readyok\n";
    std::cout.flush();
}

void UCI::cmd_ucinewgame() {
    engine_.reset();
    current_pos_ = Position{};
    std::cout << ANSI_GREEN << "  [New game]\n" << ANSI_RESET;
    std::cout.flush();
}

void UCI::cmd_display() { print_board(current_pos_); }

void UCI::cmd_eval() {
    int score = evaluate(current_pos_);
    std::cout << "  Static eval: " << format_score(score, 0) << "\n";
    int ct = count_threats(current_pos_.current,  current_pos_.opponent());
    int ot = count_threats(current_pos_.opponent(), current_pos_.current);
    std::cout << "  My threats: " << ct << "  Opp threats: " << ot << "\n";
    std::cout.flush();
}

void UCI::cmd_help() {
    std::cout << ANSI_BOLD
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
    std::cout.flush();
}

// ─────────────────────────────────────────────
//  setoption name <Name> value <Value>
//
//  Currently supported:
//    Hash    — TT size in MB
//    Threads — 0 = auto-detect all CPU cores on
//              this machine (default), N = pin to
//              exactly N threads
// ─────────────────────────────────────────────
void UCI::cmd_setoption(const std::string& line) {
    size_t np = line.find("name ");
    size_t vp = line.find(" value ");
    if (np == std::string::npos) { std::cout << "  [Error] Bad setoption\n"; return; }

    std::string name = (vp != std::string::npos)
        ? line.substr(np + 5, vp - (np + 5))
        : line.substr(np + 5);
    // trim trailing/leading spaces
    while (!name.empty() && name.back() == ' ')  name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());

    std::string value = (vp != std::string::npos) ? line.substr(vp + 7) : "";

    if (name == "Threads") {
        int n = 0;
        try { n = std::stoi(value); } catch (...) { n = 0; }
        if (n < 0) n = 0;
        engine_.set_threads(static_cast<unsigned>(n));
        if (n == 0) {
            std::cout << "  [Threads] auto (will use all "
                      << Engine::hardware_threads() << " detected cores)\n";
        } else {
            std::cout << "  [Threads] pinned to " << n << "\n";
        }
    } else if (name == "Hash") {
        std::cout << "  [Hash] note: Hash size takes effect on next engine restart\n";
    } else {
        std::cout << "  [Error] Unknown option: " << name << "\n";
    }
    std::cout.flush();
}

bool UCI::apply_moves(const std::string& moves_str) {
    std::istringstream iss(moves_str);
    std::string token;
    while (iss >> token) {
        if (token.size() == 1 && token[0] >= '0' && token[0] <= '6') {
            int col = token[0] - '0';
            if (!current_pos_.can_play(col)) {
                std::cout << "  [Error] Column " << col << " full\n";
                return false;
            }
            current_pos_ = current_pos_.after(col);
        } else {
            std::cout << "  [Error] Bad move: " << token << "\n";
            return false;
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
        if (!set_fen(fs)) { std::cout << "  [Error] Bad FEN\n"; return; }
    }
    if (mp != std::string::npos)
        apply_moves(line.substr(mp + 7));
}

bool UCI::set_fen(const std::string& fen) {
    current_pos_ = Position{};
    std::istringstream iss(fen);
    std::string board_str;
    if (!(iss >> board_str)) return false;
    int row = ROWS - 1, col = 0;
    for (char ch : board_str) {
        if (ch == '/') { --row; col = 0; if (row < 0) break; }
        else if (ch == '.' || ch == '-') { ++col; }
        else if (ch == 'X' || ch == 'x' || ch == '1') {
            if (row >= 0 && col < COLS)
                current_pos_.mask |= BB(1) << (col + row * COLS);
            ++col;
        } else if (ch == 'O' || ch == 'o' || ch == '2') {
            if (row >= 0 && col < COLS)
                current_pos_.mask |= BB(1) << (col + row * COLS);
            ++col;
        }
    }
    return true;
}

// ─────────────────────────────────────────────
//  cmd_go
//
//  Live thread HATA diya — sirf depth lines print hoti hain.
//  Infinite mode mein search_thread background mein chalta hai.
//  Stop/Enter dono se cleanly ruk jaata hai.
// ─────────────────────────────────────────────
void UCI::cmd_go(const std::string& line) {
    // Always fully join the previous search thread before touching
    // search_thread_ again — even if it already finished on its own
    // (e.g. an unlimited search auto-stops as soon as it finds a
    // forced win/loss, which sets searching_ = false without ever
    // joining the thread). Re-assigning std::thread to an object that
    // still represents a joinable thread calls std::terminate() and
    // kills the whole process — this was the cause of the crash on
    // the 2nd "go infinite" after a mate was found.
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

    if (unlimited) {
        std::cout << ANSI_CYAN << ANSI_BOLD
                  << "\n  Searching... (type 'stop' or press ENTER to halt)\n\n"
                  << ANSI_RESET;
    } else {
        std::cout << ANSI_CYAN << ANSI_BOLD
                  << "\n  Searching depth " << fixed_depth << "...\n\n"
                  << ANSI_RESET;
    }
    std::cout << ANSI_GRAY << "  [Using " << ANSI_RESET << ANSI_BOLD
              << engine_.configured_threads() << ANSI_RESET << ANSI_GRAY
              << " CPU thread(s) on this machine]\n" << ANSI_RESET;
    std::cout.flush();
    print_board(current_pos_);

    Position pos_copy = current_pos_;
    searching_ = true;

    // Reset engine (clears stop flag from previous search)
    engine_.reset();
    current_pos_ = pos_copy;

    search_thread_ = std::thread([this, pos_copy, fixed_depth]() {
        SearchResult result = engine_.search(pos_copy, fixed_depth);

        std::cout << "\n" << ANSI_BOLD << ANSI_GREEN
                  << "  ══ Done ═══════════════════════════════════\n" << ANSI_RESET;
        print_final_result(result);
        searching_ = false;
    });

    if (!unlimited) {
        search_thread_.join();
    }
    // unlimited: returns immediately, search runs in background
}

void UCI::print_final_result(const SearchResult& result) {
    // A search that got interrupted (e.g. a new 'go' arrived before this
    // one finished even depth 1) returns depth=0/best_move=-1/score=0 —
    // identical in shape to a genuine draw. Label it explicitly so it's
    // never confused with a real evaluation.
    if (result.depth == 0 && result.best_move < 0) {
        std::cout << "  " << ANSI_YELLOW
                  << "[Search interrupted before completing — no result. "
                     "Wait for a search to finish before sending another 'go'.]"
                  << ANSI_RESET << "\n";
        return;
    }

    std::cout << "  bestmove " << ANSI_BOLD << ANSI_MAGENTA;
    if (result.best_move >= 0) std::cout << result.best_move;
    else                       std::cout << "(none)";
    std::cout << ANSI_RESET << "\n"
              << "  score    " << format_score(result.score, result.mate_in) << "\n"
              << "  depth    " << result.depth << "\n";

    const SearchStats& s = engine_.stats();
    uint64_t nodes  = s.nodes_explored.load();
    uint64_t pruned = s.nodes_pruned.load();
    double pct_p = (nodes + pruned) > 0 ? 100.0*pruned/(nodes+pruned) : 0.0;

    std::cout << "  nodes    " << nodes << "\n"
              << "  pruned   " << pruned
              << " (" << std::fixed << std::setprecision(1) << pct_p << "%)\n"
              << "  tt_hits  " << s.tt_hits.load() << "\n"
              << "  threads  " << engine_.threads_used() << "\n";

    if (!result.pv.empty()) {
        std::cout << "  pv      ";
        for (int c : result.pv) std::cout << " " << c;
        std::cout << "\n";
    }

    if (std::abs(result.score) >= MATE_THRESHOLD) {
        if (result.score > 0)
            std::cout << "\n  " << ANSI_GREEN << ANSI_BOLD
                      << "★ FORCED WIN in " << result.mate_in << " move(s)!"
                      << ANSI_RESET << "\n";
        else
            std::cout << "\n  " << ANSI_RED << ANSI_BOLD
                      << "✗ FORCED LOSS in " << -result.mate_in << " move(s)"
                      << ANSI_RESET << "\n";
    }

    std::cout << ANSI_BOLD << ANSI_GREEN
              << "  ═══════════════════════════════════════════\n\n"
              << ANSI_RESET;
    std::cout.flush();
}

void UCI::cmd_move(int col) {
    if (col < 0 || col >= COLS) {
        std::cout << "  [Error] Column 0-6\n"; return;
    }
    if (!current_pos_.can_play(col)) {
        std::cout << "  [Error] Column " << col << " full\n"; return;
    }
    if (is_win(current_pos_.current) || is_win(current_pos_.opponent())) {
        std::cout << "  [Error] Game over\n"; return;
    }

    std::string player = (current_pos_.side_to_move() == 0)
        ? std::string(ANSI_YELLOW) + "P1" + ANSI_RESET
        : std::string(ANSI_RED)    + "P2" + ANSI_RESET;
    std::cout << "  " << player << " plays col " << ANSI_BOLD << col << ANSI_RESET << "\n";
    current_pos_ = current_pos_.after(col);
    print_board(current_pos_);

    if (is_win(current_pos_.opponent())) {
        std::string w = (current_pos_.side_to_move() == 0)
            ? std::string(ANSI_RED)    + "P2 WINS!" + ANSI_RESET
            : std::string(ANSI_YELLOW) + "P1 WINS!" + ANSI_RESET;
        std::cout << "  " << ANSI_BOLD << w << "\n\n"; return;
    }
    if (current_pos_.is_full())
        std::cout << "  " << ANSI_WHITE << ANSI_BOLD << "DRAW!" << ANSI_RESET << "\n\n";
}

void UCI::cmd_stop() {
    if (!searching_) return;
    engine_.stop();
    if (search_thread_.joinable()) search_thread_.join();
    searching_ = false;
    std::cout << "  [Stopped]\n";
    std::cout.flush();
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
    std::cout << "  Perft(" << depth << ")...\n";
    auto t0 = std::chrono::high_resolution_clock::now();
    uint64_t n = perft_inner(current_pos_, depth);
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t0).count();
    std::cout << "  Nodes: " << n
              << "  Time: " << std::fixed << std::setprecision(1) << ms << "ms\n";
    std::cout.flush();
}

// ─────────────────────────────────────────────
//  Main loop
// ─────────────────────────────────────────────
void UCI::run() {
    std::cout << ANSI_BOLD << ANSI_CYAN
              << "\n  Connect-4 Engine v1.0\n"
              << "  Negamax + Alpha-Beta + TT (Lazy SMP)\n"
              << ANSI_RESET
              << "  Detected " << ANSI_BOLD << Engine::hardware_threads() << ANSI_RESET
              << " CPU thread(s) on this machine — search will auto-scale to use them.\n"
              << "  Type " << ANSI_YELLOW << "help" << ANSI_RESET << "\n\n";
    std::cout.flush();

    std::string line;
    while (!quit_flag_) {
        if (!searching_) {
            std::cout << ANSI_GRAY << "> " << ANSI_RESET;
            std::cout.flush();
        }

        if (!std::getline(std::cin, line)) break;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        if (line.empty()) {
            if (searching_) cmd_stop();
            continue;
        }

        if      (line == "uci")                    cmd_uci();
        else if (line == "isready")                cmd_isready();
        else if (line == "ucinewgame")             cmd_ucinewgame();
        else if (line == "display" || line == "d") cmd_display();
        else if (line == "eval"    || line == "e") cmd_eval();
        else if (line == "stop")                   cmd_stop();
        else if (line == "quit" || line == "exit" || line == "q") cmd_quit();
        else if (line == "help" || line == "h")    cmd_help();
        else if (line.substr(0, 8) == "position") cmd_position(line);
        else if (line.substr(0, 9) == "setoption") cmd_setoption(line);
        else if (line.substr(0, 2) == "go")       cmd_go(line);
        else if (line.substr(0, 5) == "perft") {
            std::istringstream iss(line.substr(5));
            int d = 5; iss >> d; cmd_perft(d);
        }
        else if (line.substr(0, 4) == "move") {
            std::istringstream iss(line.substr(4));
            int col = -1; iss >> col; cmd_move(col);
        }
        else std::cout << "  Unknown: '" << line << "'\n";

        std::cout.flush();
    }
    std::cout << "\n  Goodbye!\n";
}
// ───────────────────────────────────────────────────────────────────
//  EMSCRIPTEN GLUE CODE: Isse apne purane uci.cpp ke sabse end mein jodein
// ───────────────────────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static UCI* g_wasm_uci_instance = nullptr;

extern "C" {

    EMSCRIPTEN_KEEPALIVE
    void sendUciCommand(const char* cmd_cstr) {
        if (!g_wasm_uci_instance) {
            g_wasm_uci_instance = new UCI();
        }

        std::string line(cmd_cstr);
        
        // Extra spaces aur newlines clean karne ke liye
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\n')) {
            line.pop_back();
        }

        if (line.empty()) return;

        // Commands ko sahi internal functions par redirect karna
        if (line == "uci") {
            g_wasm_uci_instance->cmd_uci();
        } else if (line == "isready") {
            g_wasm_uci_instance->cmd_isready();
        } else if (line == "ucinewgame") {
            g_wasm_uci_instance->cmd_ucinewgame();
        } else if (line == "stop") {
            g_wasm_uci_instance->cmd_stop();
        } else if (line.rfind("position", 0) == 0) {
            g_wasm_uci_instance->cmd_position(line);
        } else if (line.rfind("go", 0) == 0) {
            g_wasm_uci_instance->cmd_go(line);
        }
    }
}
#endif
