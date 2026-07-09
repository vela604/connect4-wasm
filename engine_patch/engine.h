#pragma once
#include "bitboard.h"
#include "tt.h"
#include "evaluator.h"
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <cstring>

// ─────────────────────────────────────────────
//  Search statistics (live, printed during search)
// ─────────────────────────────────────────────
struct SearchStats {
    std::atomic<uint64_t> nodes_explored{0};
    std::atomic<uint64_t> nodes_pruned{0};
    std::atomic<uint64_t> tt_hits{0};
    std::atomic<uint64_t> tt_stores{0};
    uint64_t              nodes_at_depth[CELLS + 1]{};
    int                   current_depth{0};

    void reset() {
        nodes_explored = 0;
        nodes_pruned   = 0;
        tt_hits        = 0;
        tt_stores      = 0;
        current_depth  = 0;
        std::fill(std::begin(nodes_at_depth), std::end(nodes_at_depth), 0);
    }

    std::string summary() const;
};

// ─────────────────────────────────────────────
//  Per-thread search context
//
//  Killer-move and history-heuristic tables used
//  to be single shared arrays on Engine. With
//  multiple search threads running concurrently
//  (one per CPU core — see Engine's thread-count
//  auto-detection below) sharing those arrays
//  would be a data race. Each worker thread now
//  gets its own ThreadContext instead, while the
//  transposition table (tt.h) — the thing that
//  actually needs to be shared for threads to
//  help each other — is lock-free/atomic and
//  safe to share.
// ─────────────────────────────────────────────
struct ThreadContext {
    int killer_moves[CELLS + 1][2];   // [depth][2 slots]
    int history[COLS][CELLS + 1];     // [col][depth]

    void clear() {
        std::memset(killer_moves, -1, sizeof(killer_moves));
        std::memset(history, 0, sizeof(history));
    }
};

// ─────────────────────────────────────────────
//  Search result for one depth
// ─────────────────────────────────────────────
struct SearchResult {
    int  best_move    = -1;
    int  score        = 0;
    int  depth        = 0;
    bool is_exact     = false;   // score is exact (not bound)
    int  mate_in      = 0;       // 0 = no forced mate, >0 = win in N, <0 = lose in N

    // Principal variation (sequence of best moves)
    std::vector<int> pv;
};

// ─────────────────────────────────────────────
//  Engine
// ─────────────────────────────────────────────
class Engine {
public:
    explicit Engine(size_t tt_size_mb = 256);
    ~Engine() = default;

    // ── Main search entry ────────────────────
    // fixed_depth == 0 → unlimited (search until stop())
    // fixed_depth >  0 → search exactly that depth
    //
    // Automatically uses all available CPU cores on
    // whatever machine it's running on (phone, laptop,
    // server, ...) via hardware_threads(), unless
    // overridden with set_threads().
    SearchResult search(const Position& pos, int fixed_depth = 0);

    // Stop ongoing search (call from another thread or signal)
    void stop();

    // Reset engine state (clear TT, stats)
    void reset();

    // Access stats
    const SearchStats& stats() const { return stats_; }

    // Callback invoked after each depth level completes (for live display)
    // Arguments: (result_so_far, stats)
    using ProgressCallback = std::function<void(const SearchResult&, const SearchStats&)>;
    void set_progress_callback(ProgressCallback cb) { progress_cb_ = std::move(cb); }

    // TT info
    double tt_usage_pct() const { return tt_.usage(); }

    // ── Thread control ───────────────────────
    // Number of logical CPU cores detected on THIS machine at
    // runtime (works the same way on a phone or a PC — it just
    // asks the OS). This is what lets the engine automatically
    // scale up on a beefy PC and scale down on a phone, with no
    // recompilation or platform-specific code needed.
    static unsigned hardware_threads();

    // 0 (default) = auto-detect via hardware_threads().
    // Any other value pins the engine to exactly that many threads
    // (e.g. to leave headroom for the UI on a phone, or to match a
    // specific core count).
    void set_threads(unsigned n) { threads_override_ = n; }

    // What search() will actually use right now (resolves the
    // override against hardware_threads() if no override is set).
    unsigned configured_threads() const {
        return threads_override_ > 0 ? threads_override_ : hardware_threads();
    }

    // Threads actually used by the most recent search() call.
    unsigned threads_used() const { return threads_used_.load(std::memory_order_relaxed); }

    // ── Stepped (resumable) search — no std::thread required ──
    // For environments with no real threading (e.g. a single-threaded
    // WebAssembly build, which needs no SharedArrayBuffer/COOP/COEP
    // headers at all). Instead of search() blocking until the whole
    // "infinite" search finishes, the caller drives it one depth at a
    // time: start_stepped_search() once, then step_once() repeatedly
    // (e.g. from a JS setTimeout(...,0) loop) until it returns false.
    // progress_cb_ still fires after every completed depth exactly as
    // it does for search(), so callers don't need to change how they
    // consume results.
    void start_stepped_search(const Position& pos, int fixed_depth = 0);
    bool step_once();                                   // returns false when done
    bool stepped_search_active() const { return step_state_.active; }
    const SearchResult& stepped_result() const { return step_state_.best_result; }

private:
    // ── Negamax with Alpha-Beta ──────────────
    // root_moves = pos.moves of the position the CURRENT search call
    // started from (i.e. the root of this negamax tree). Needed to
    // correctly compute "plies from root until mate" for the
    // WIN/LOSS-in-N display — using pos.moves directly (total stones
    // since game start) instead would report how many cells are left
    // on the whole board, not how many moves away the forced result
    // actually is.
    int negamax(Position pos, int depth, int alpha, int beta,
                std::vector<int>& pv, ThreadContext& ctx, int root_moves, bool use_tt = true);

    // ── Quiescence search for tactical stability ──
    int quiescence(Position pos, int alpha, int beta, int root_moves);

    // ── Move ordering ────────────────────────
    // Returns ordered list: winning moves → threatening moves → center-first → rest
    std::vector<int> order_moves(const Position& pos, int tt_best_move,
                                  const ThreadContext& ctx) const;

    // Check if move creates an immediate win
    bool is_winning_move(const Position& pos, int col) const;

    // Check if opponent wins if we play col (i.e., we must block)
    bool is_opponent_winning(const Position& pos, int col) const;

    // ── Principal variation extraction ───────
    void extract_pv(Position pos, std::vector<int>& pv, int depth);

    // ── Mate score helpers ───────────────────
    static int  mate_score(int moves_left);
    static bool is_mate_score(int score);
    static int  moves_to_mate(int score, int moves_played);

    // ── One thread's iterative-deepening loop ─
    // report=true → this is the "main" thread: updates progress_cb_
    // and produces the SearchResult that gets returned to the caller.
    // report=false → this is a helper thread that just searches to
    // help populate the shared transposition table; its return value
    // is discarded.
    SearchResult iterative_deepen(const Position& pos, int max_depth, int fixed_depth,
                                   ThreadContext& ctx, bool report);

    TranspositionTable   tt_;
    SearchStats          stats_;
    std::atomic<bool>    stop_flag_{false};
    ProgressCallback     progress_cb_;

    unsigned              threads_override_{0};  // 0 = auto
    std::atomic<unsigned> threads_used_{1};

    // State for the stepped/resumable search API (see start_stepped_search).
    struct StepState {
        Position       pos;
        ThreadContext  ctx;
        int            depth       = 0;
        int            max_depth   = 0;
        int            fixed_depth = 0;
        SearchResult   best_result;
        bool           active      = false;
    };
    StepState step_state_;

    static void update_killer(ThreadContext& ctx, int col, int depth);
    static void update_history(ThreadContext& ctx, int col, int depth);
};
