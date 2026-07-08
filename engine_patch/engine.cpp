#include "engine.h"
#include <algorithm>
#include <sstream>
#include <cstring>
#include <iostream>
#include <iomanip>

// ─────────────────────────────────────────────
//  SearchStats::summary
// ─────────────────────────────────────────────
std::string SearchStats::summary() const {
    std::ostringstream oss;
    uint64_t total = nodes_explored.load();
    uint64_t pruned = nodes_pruned.load();
    double pct_pruned = total > 0 ? 100.0 * pruned / (total + pruned) : 0.0;
    oss << "Nodes: " << total
        << "  Pruned: " << pruned
        << " (" << std::fixed << std::setprecision(1) << pct_pruned << "%)"
        << "  TT Hits: " << tt_hits.load()
        << "  TT Stores: " << tt_stores.load();
    return oss.str();
}

// ─────────────────────────────────────────────
//  Engine constructor
// ─────────────────────────────────────────────
Engine::Engine(size_t tt_size_mb)
    : tt_(tt_size_mb)
{
}

void Engine::reset() {
    tt_.clear();
    stats_.reset();
    stop_flag_ = false;
}

void Engine::stop() {
    stop_flag_ = true;
}

unsigned Engine::hardware_threads() {
    unsigned n = std::thread::hardware_concurrency();
    return n > 0 ? n : 1;  // hardware_concurrency() may return 0 if undetectable
}

void Engine::update_killer(ThreadContext& ctx, int col, int depth) {
    if (depth < 0 || depth > CELLS) return;
    if (ctx.killer_moves[depth][0] != col) {
        ctx.killer_moves[depth][1] = ctx.killer_moves[depth][0];
        ctx.killer_moves[depth][0] = col;
    }
}

void Engine::update_history(ThreadContext& ctx, int col, int depth) {
    if (col < 0 || col >= COLS) return;
    if (depth < 0 || depth > CELLS) return;
    ctx.history[col][depth] += depth * depth;  // bonus proportional to depth squared
}

// ─────────────────────────────────────────────
//  Mate score helpers
// ─────────────────────────────────────────────
int Engine::mate_score(int moves_left) {
    return SCORE_WIN - moves_left;
}

bool Engine::is_mate_score(int score) {
    return std::abs(score) >= MATE_THRESHOLD;
}

int Engine::moves_to_mate(int score, int /*moves_played*/) {
    // Positive score = current player wins
    // SCORE_WIN - moves_left = score
    // moves_left = SCORE_WIN - score
    // total_moves = moves_played + moves_left
    if (score > 0) {
        int moves_left = SCORE_WIN - score;
        return moves_left;
    } else {
        int moves_left = SCORE_WIN + score;
        return -moves_left;
    }
}

// ─────────────────────────────────────────────
//  Move ordering
// ─────────────────────────────────────────────
bool Engine::is_winning_move(const Position& pos, int col) const {
    return pos.is_winning_move(col);
}

bool Engine::is_opponent_winning(const Position& pos, int col) const {
    // If we play col, does opponent win on the NEXT move somewhere?
    // Actually this checks: after playing col, does opponent have immediate win?
    // We use it to detect: if we DON'T play col, opponent wins here
    // i.e., is col a blocking move?
    // Better: check if the cell above col is an opponent winning square
    Position after = pos.after(col);
    // In 'after', it's opponent's turn (they became 'current')
    // Check if opponent (now 'current' in after) has immediate win
    for (int c = 0; c < COLS; ++c) {
        if (after.can_play(c) && after.is_winning_move(c)) return true;
    }
    return false;
}

std::vector<int> Engine::order_moves(const Position& pos, int tt_best_move,
                                      const ThreadContext& ctx) const {
    struct ScoredMove {
        int col;
        int priority;
    };

    std::vector<ScoredMove> moves;
    moves.reserve(COLS);

    for (int col = 0; col < COLS; ++col) {
        if (!pos.can_play(col)) continue;

        int priority = 0;

        // Priority 1: Immediate win (highest)
        if (pos.is_winning_move(col)) {
            priority = 100'000;
        }
        // Priority 2: TT best move
        else if (col == tt_best_move) {
            priority = 90'000;
        }
        // Priority 3: Block opponent win
        else {
            // Check if opponent would win here if we don't play
            // (i.e., this is a forced blocking move)
            // Check if opponent currently threatens a win in col
            bool is_block = false;
            for (int oc = 0; oc < COLS; ++oc) {
                // Would opponent win if they played oc right now (before our move)?
                if (pos.can_play(oc)) {
                    BB opp = pos.opponent();
                    int or_ = pos.height(oc);
                    BB bit = BB(1) << (oc + or_ * COLS);
                    if (is_win(opp | bit) && oc == col) {
                        is_block = true;
                        break;
                    }
                }
            }
            if (is_block) {
                priority = 80'000;
            } else {
                // Priority 4: Killer moves
                int depth_approx = CELLS - pos.moves;
                if (depth_approx >= 0 && depth_approx <= CELLS) {
                    if (ctx.killer_moves[depth_approx][0] == col) priority += 5'000;
                    else if (ctx.killer_moves[depth_approx][1] == col) priority += 4'000;
                }
                // Priority 5: History heuristic
                if (depth_approx >= 0 && depth_approx <= CELLS) {
                    priority += ctx.history[col][depth_approx];
                }
                // Priority 6: Center preference
                priority += CENTER_COL_WEIGHT[col] * 100;

                // Priority 7: Don't play below opponent's winning square
                // (playing col would give opponent a win above us)
                int h = pos.height(col);
                if (h + 1 < ROWS) {
                    BB above_bit = BB(1) << (col + (h + 1) * COLS);
                    BB opp = pos.opponent();
                    if (is_win(opp | above_bit)) {
                        priority -= 50'000;  // very bad move — don't give opponent win
                    }
                }
            }
        }

        moves.push_back({col, priority});
    }

    std::stable_sort(moves.begin(), moves.end(),
        [](const ScoredMove& a, const ScoredMove& b) {
            return a.priority > b.priority;
        });

    std::vector<int> result;
    result.reserve(moves.size());
    for (auto& m : moves) result.push_back(m.col);
    return result;
}

// ─────────────────────────────────────────────
//  Principal variation extraction
// ─────────────────────────────────────────────
void Engine::extract_pv(Position pos, std::vector<int>& pv, int depth) {
    if (depth <= 0 || pos.is_full()) return;
    if (is_win(pos.current) || is_win(pos.opponent())) return;

    uint64_t key = pos.canonical_key();
    TTEntry entry;
    if (!tt_.probe(key, entry) || entry.best_move < 0) return;

    int col = entry.best_move;
    if (!pos.can_play(col)) return;

    pv.push_back(col);
    pos = pos.after(col);
    extract_pv(pos, pv, depth - 1);
}

// ─────────────────────────────────────────────
//  Quiescence search
//  Only check immediate wins/losses
// ─────────────────────────────────────────────
int Engine::quiescence(Position pos, int alpha, int beta, int root_moves) {
    ++stats_.nodes_explored;

    // Check for immediate wins by current player
    for (int c = 0; c < COLS; ++c) {
        if (pos.can_play(c) && pos.is_winning_move(c)) {
            int ply = pos.moves - root_moves;  // plies played since root
            return mate_score(ply + 1);         // this move (ply+1) completes the win
        }
    }

    // Static eval
    int stand_pat = evaluate(pos);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    return alpha;
}

// ─────────────────────────────────────────────
//  Negamax with Alpha-Beta Pruning
//
//  Returns score from perspective of current player.
//  Positive = current player winning.
// ─────────────────────────────────────────────
int Engine::negamax(Position pos, int depth, int alpha, int beta,
                    std::vector<int>& pv, ThreadContext& ctx, int root_moves, bool use_tt)
{
    if (stop_flag_) return 0;

    ++stats_.nodes_explored;

    int ply = pos.moves - root_moves;  // plies played since this search's root

    // ── Terminal checks ───────────────────────
    // Current player already won? (shouldn't happen in normal flow
    // since we detect wins before recursing, but guard anyway).
    // Their own stones can only already contain a win from their move
    // 2 plies back (their last move), so the mate completed at ply-1.
    if (is_win(pos.current)) {
        return mate_score(std::max(ply - 1, 0));
    }
    // Opponent (the side that just moved to reach this node) already
    // won — that win was completed exactly `ply` plies from root.
    if (is_win(pos.opponent())) {
        return -mate_score(ply);
    }
    if (pos.is_full()) {
        return SCORE_DRAW;
    }

    // ── Depth limit ───────────────────────────
    if (depth <= 0) {
        return quiescence(pos, alpha, beta, root_moves);
    }

    // ── Transposition Table probe ─────────────
    uint64_t key = pos.canonical_key();
    int tt_best_move = -1;

    if (use_tt) {
        TTEntry entry;
        bool found = tt_.probe(key, entry);
        if (found && entry.depth >= depth) {
            ++stats_.tt_hits;
            int tt_score = entry.score;
            if (entry.flag == TTFlag::EXACT) {
                pv.clear();
                if (entry.best_move >= 0) pv.push_back(entry.best_move);
                return tt_score;
            } else if (entry.flag == TTFlag::LOWER) {
                alpha = std::max(alpha, tt_score);
            } else if (entry.flag == TTFlag::UPPER) {
                beta = std::min(beta, tt_score);
            }
            if (alpha >= beta) {
                ++stats_.nodes_pruned;
                return tt_score;
            }
            tt_best_move = entry.best_move;
        } else if (found) {
            tt_best_move = entry.best_move;  // use move hint even if depth insufficient
        }
    }

    // ── Win in 1 check (before full move ordering) ────────
    // Quick scan for immediate wins
    for (int c : COL_ORDER) {
        if (pos.can_play(c) && pos.is_winning_move(c)) {
            int score = mate_score(ply + 1);  // this move (ply+1) completes the win
            // Store in TT
            if (use_tt) {
                tt_.store(key, score, depth, c, TTFlag::EXACT);
                ++stats_.tt_stores;
            }
            pv.clear();
            pv.push_back(c);
            return score;
        }
    }

    // ── Check if opponent has winning threats we must block ────
    // Count opponent winning moves
    std::vector<int> forced_blocks;
    for (int c = 0; c < COLS; ++c) {
        if (!pos.can_play(c)) continue;
        BB opp = pos.opponent();
        int r = pos.height(c);
        BB bit = BB(1) << (c + r * COLS);
        if (is_win(opp | bit)) forced_blocks.push_back(c);
    }

    // If opponent has 2+ threats we can't block all → we lose
    if (forced_blocks.size() >= 2) {
        // We make one more move (ply+1), then opponent wins (ply+2).
        int score = -mate_score(ply + 2);
        if (use_tt) {
            tt_.store(key, score, depth, forced_blocks[0], TTFlag::EXACT);
            ++stats_.tt_stores;
        }
        return score;
    }

    // ── Move ordering ─────────────────────────
    std::vector<int> ordered_moves;
    if (!forced_blocks.empty()) {
        // Must play the only blocking move
        ordered_moves = forced_blocks;
    } else {
        ordered_moves = order_moves(pos, tt_best_move, ctx);
    }

    // ── Alpha-Beta search ─────────────────────
    int  best_score = std::numeric_limits<int>::min() + 1;
    int  best_move  = -1;
    TTFlag flag     = TTFlag::UPPER;
    std::vector<int> best_child_pv;

    for (int col : ordered_moves) {
        if (stop_flag_) break;

        std::vector<int> child_pv;
        Position next = pos.after(col);

        // Negamax: negate opponent's score
        int score = -negamax(next, depth - 1, -beta, -alpha, child_pv, ctx, root_moves, use_tt);

        if (score > best_score) {
            best_score    = score;
            best_move     = col;
            best_child_pv = child_pv;
        }

        if (score > alpha) {
            alpha = score;
            flag  = TTFlag::EXACT;
        }

        if (alpha >= beta) {
            // Beta cutoff — prune remaining
            ++stats_.nodes_pruned;
            flag = TTFlag::LOWER;
            update_killer(ctx, col, depth);
            update_history(ctx, col, depth);
            break;
        }
    }

    // ── Store in TT ───────────────────────────
    if (!stop_flag_ && use_tt && best_move >= 0) {
        tt_.store(key, best_score, depth, best_move, flag);
        ++stats_.tt_stores;
    }

    // ── Build PV ─────────────────────────────
    pv.clear();
    if (best_move >= 0) {
        pv.push_back(best_move);
        pv.insert(pv.end(), best_child_pv.begin(), best_child_pv.end());
    }

    return best_score;
}

// ─────────────────────────────────────────────
//  One thread's iterative-deepening loop
// ─────────────────────────────────────────────
SearchResult Engine::iterative_deepen(const Position& pos, int max_depth, int fixed_depth,
                                       ThreadContext& ctx, bool report) {
    SearchResult best_result;
    best_result.best_move = -1;
    best_result.score     = 0;
    best_result.depth     = 0;

    for (int depth = 1; depth <= max_depth; ++depth) {
        if (stop_flag_) break;

        if (report) stats_.current_depth = depth;

        std::vector<int> pv;
        int score = negamax(pos, depth, SCORE_LOSS - 1, SCORE_WIN + 1, pv, ctx, pos.moves);

        if (stop_flag_) break;

        // Update result
        best_result.depth     = depth;
        best_result.score     = score;
        best_result.is_exact  = true;
        best_result.pv        = pv;
        best_result.best_move = pv.empty() ? -1 : pv[0];

        // Compute mate distance
        if (is_mate_score(score)) {
            best_result.mate_in = moves_to_mate(score, pos.moves);
        } else {
            best_result.mate_in = 0;
        }

        // Notify caller (live display) — only the reporting thread does this
        if (report && progress_cb_) progress_cb_(best_result, stats_);

        // If we found a forced win/loss, no need to go deeper
        if (is_mate_score(score)) {
            if (fixed_depth == 0) break;  // unlimited mode: stop on mate
        }

        // If fixed depth reached, stop
        if (fixed_depth > 0 && depth >= fixed_depth) break;
    }

    return best_result;
}

// ─────────────────────────────────────────────
//  Main search entry
//
//  Automatically scales across however many CPU
//  cores the current machine (phone, laptop,
//  desktop, server...) actually has, detected at
//  runtime via hardware_threads(). This is a
//  "Lazy SMP" scheme: every thread independently
//  runs the same iterative-deepening negamax
//  search on the SAME root position, all sharing
//  one lock-free transposition table. Helper
//  threads' search results aren't used directly —
//  their value comes from filling the shared TT
//  with positions faster than one thread could
//  alone, which speeds up the main thread's own
//  search via more TT hits/cutoffs. Helper threads
//  start at slightly staggered depths so they
//  don't all walk in exact lockstep.
// ─────────────────────────────────────────────
SearchResult Engine::search(const Position& pos, int fixed_depth) {
    stop_flag_ = false;
    stats_.reset();

    SearchResult empty_result;
    empty_result.best_move = -1;
    empty_result.score     = 0;
    empty_result.depth     = 0;

    // Quick sanity: is game already over?
    if (is_win(pos.current) || is_win(pos.opponent()) || pos.is_full()) {
        return empty_result;
    }

    int max_depth = (fixed_depth > 0) ? fixed_depth : CELLS;

    unsigned n_threads = (threads_override_ > 0) ? threads_override_ : hardware_threads();
    if (n_threads < 1) n_threads = 1;
    threads_used_.store(n_threads, std::memory_order_relaxed);

    // ── Single-threaded path (e.g. low-end device, or explicit override) ──
    if (n_threads == 1) {
        ThreadContext ctx;
        ctx.clear();
        return iterative_deepen(pos, max_depth, fixed_depth, ctx, /*report=*/true);
    }

    // ── Multi-threaded path: use every other core to help ──
    std::vector<std::thread> helpers;
    helpers.reserve(n_threads - 1);

    for (unsigned i = 1; i < n_threads; ++i) {
        helpers.emplace_back([this, pos, max_depth, i]() {
            ThreadContext ctx;
            ctx.clear();
            // Stagger each helper's starting depth a little so threads
            // diversify instead of all exploring in identical order.
            int start_depth = 1 + static_cast<int>(i % 3);
            for (int depth = start_depth; depth <= max_depth && !stop_flag_; ++depth) {
                std::vector<int> pv;
                negamax(pos, depth, SCORE_LOSS - 1, SCORE_WIN + 1, pv, ctx, pos.moves);
            }
        });
    }

    ThreadContext main_ctx;
    main_ctx.clear();
    SearchResult result = iterative_deepen(pos, max_depth, fixed_depth, main_ctx, /*report=*/true);

    // Main thread is done (hit mate, hit fixed depth, or was stopped) —
    // signal helpers to wind down and wait for them.
    bool was_stopped = stop_flag_;
    stop_flag_ = true;
    for (auto& t : helpers) t.join();
    if (!was_stopped) stop_flag_ = false;  // restore, in case caller checks it next

    return result;
}
