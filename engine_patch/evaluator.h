#pragma once
#include "bitboard.h"
#include <array>

// ─────────────────────────────────────────────
//  Static Evaluation Function
//
//  Returns score from the perspective of the
//  player who is currently to move.
//  Positive = good for current player
//  Negative = good for opponent
// ─────────────────────────────────────────────

// ── Pattern weights ──────────────────────────
// Scores for windows of 4 cells

// Score for having N pieces in a window of 4 (rest empty)
static constexpr int WINDOW_SCORE[5] = {
    0,    // 0 pieces — neutral
    2,    // 1 piece  — slight positional value
    10,   // 2 pieces — threat developing
    80,   // 3 pieces — strong threat
    0     // 4 pieces — handled as terminal win
};

// Penalty when opponent has N pieces in a window (rest empty)
static constexpr int BLOCK_SCORE[5] = {
    0,
    1,
    8,
    60,
    0
};

// Center column weights (reward center control)
// col:      0   1   2   3   4   5   6
static constexpr int CENTER_COL_WEIGHT[COLS] = {
    1, 2, 4, 8, 4, 2, 1
};

// Center row weights (lower rows = more stable)
// row:      0   1   2   3   4   5
static constexpr int CENTER_ROW_WEIGHT[ROWS] = {
    3, 4, 5, 4, 3, 2
};

// ── Odd-Even threat helpers ──────────────────
// In Connect-4, odd threats (row 1,3,5 from bottom=0) are special
// because the game alternates turns. P1 (first mover) wants to control
// odd rows (1-indexed rows 1,3,5 → 0-indexed 0,2,4)
// P2 wants to control even rows (0-indexed 1,3,5)
// A threat in an "odd" row for P1 means P1 can complete it on their turn.

inline int row_threat_bonus(int row, int side_to_move) {
    // side_to_move: 0=P1(first), 1=P2(second)
    // Odd rows (0-indexed: 0,2,4) favor P1
    // Even rows (0-indexed: 1,3,5) favor P2
    bool is_odd_indexed = (row % 2 == 0);  // 0-indexed row 0,2,4
    if (side_to_move == 0 && is_odd_indexed)  return 3;
    if (side_to_move == 1 && !is_odd_indexed) return 3;
    return 0;
}

// ── Window evaluation ────────────────────────
// Count pieces in a 4-cell window
struct WindowCount {
    int current_pieces;
    int opponent_pieces;
    int empty;
};

inline WindowCount count_window(BB current, BB opponent, BB window_mask) {
    WindowCount wc;
    BB cur_in  = current  & window_mask;
    BB opp_in  = opponent & window_mask;
    BB emp_in  = window_mask & ~(current | opponent);
    wc.current_pieces  = __builtin_popcountll(cur_in);
    wc.opponent_pieces = __builtin_popcountll(opp_in);
    wc.empty           = __builtin_popcountll(emp_in);
    return wc;
}

inline int score_window(const WindowCount& wc, int row_bonus) {
    int score = 0;
    // Our window
    if (wc.current_pieces > 0 && wc.opponent_pieces == 0) {
        score += WINDOW_SCORE[wc.current_pieces] + row_bonus;
    }
    // Their window
    if (wc.opponent_pieces > 0 && wc.current_pieces == 0) {
        score -= BLOCK_SCORE[wc.opponent_pieces] + row_bonus;
    }
    return score;
}

// ── Threat detection ─────────────────────────
// Count how many "open threes" current player has
inline int count_threats(BB current, BB opponent) {
    int threats = 0;

    // Helper: check if a 4-window has exactly 3 current + 0 opponent
    auto is_threat_window = [&](BB window) -> bool {
        BB cur_in = current  & window;
        BB opp_in = opponent & window;
        return (__builtin_popcountll(cur_in) == 3 && opp_in == 0);
    };

    // Horizontal
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c <= COLS - 4; ++c) {
            BB w = 0;
            for (int i = 0; i < 4; ++i) w |= BB(1) << (c + i + r * COLS);
            if (is_threat_window(w)) ++threats;
        }
    }
    // Vertical
    for (int c = 0; c < COLS; ++c) {
        for (int r = 0; r <= ROWS - 4; ++r) {
            BB w = 0;
            for (int i = 0; i < 4; ++i) w |= BB(1) << (c + (r + i) * COLS);
            if (is_threat_window(w)) ++threats;
        }
    }
    // Diagonal /
    for (int r = 0; r <= ROWS - 4; ++r) {
        for (int c = 0; c <= COLS - 4; ++c) {
            BB w = 0;
            for (int i = 0; i < 4; ++i) w |= BB(1) << ((c + i) + (r + i) * COLS);
            if (is_threat_window(w)) ++threats;
        }
    }
    // Diagonal backslash
    for (int r = 0; r <= ROWS - 4; ++r) {
        for (int c = 3; c < COLS; ++c) {
            BB w = 0;
            for (int i = 0; i < 4; ++i) w |= BB(1) << ((c - i) + (r + i) * COLS);
            if (is_threat_window(w)) ++threats;
        }
    }
    return threats;
}

// ── Double threat (fork) detection ───────────
// If current player has 2+ threats, opponent can't block both
inline bool has_fork(BB current, BB opponent) {
    return count_threats(current, opponent) >= 2;
}

// ─────────────────────────────────────────────
//  Main evaluation function
// ─────────────────────────────────────────────
inline int evaluate(const Position& pos) {
    // Terminal states should be caught before calling evaluate()
    // But guard anyway
    if (is_win(pos.current))   return SCORE_WIN;
    if (is_win(pos.opponent())) return SCORE_LOSS;

    int score = 0;
    BB  cur = pos.current;
    BB  opp = pos.opponent();
    int stm = pos.side_to_move();

    // ── 1. Window scoring ─────────────────────
    // Horizontal windows
    for (int r = 0; r < ROWS; ++r) {
        int rb = row_threat_bonus(r, stm);
        for (int c = 0; c <= COLS - 4; ++c) {
            BB w = 0;
            for (int i = 0; i < 4; ++i) w |= BB(1) << (c + i + r * COLS);
            auto wc = count_window(cur, opp, w);
            score += score_window(wc, rb);
        }
    }
    // Vertical windows
    for (int c = 0; c < COLS; ++c) {
        for (int r = 0; r <= ROWS - 4; ++r) {
            int rb = row_threat_bonus(r, stm);
            BB w = 0;
            for (int i = 0; i < 4; ++i) w |= BB(1) << (c + (r + i) * COLS);
            auto wc = count_window(cur, opp, w);
            score += score_window(wc, rb);
        }
    }
    // Diagonal / windows
    for (int r = 0; r <= ROWS - 4; ++r) {
        for (int c = 0; c <= COLS - 4; ++c) {
            int rb = row_threat_bonus(r, stm);
            BB w = 0;
            for (int i = 0; i < 4; ++i) w |= BB(1) << ((c + i) + (r + i) * COLS);
            auto wc = count_window(cur, opp, w);
            score += score_window(wc, rb);
        }
    }
    // Diagonal \ windows
    for (int r = 0; r <= ROWS - 4; ++r) {
        for (int c = 3; c < COLS; ++c) {
            int rb = row_threat_bonus(r, stm);
            BB w = 0;
            for (int i = 0; i < 4; ++i) w |= BB(1) << ((c - i) + (r + i) * COLS);
            auto wc = count_window(cur, opp, w);
            score += score_window(wc, rb);
        }
    }

    // ── 2. Center column priority ─────────────
    for (int c = 0; c < COLS; ++c) {
        for (int r = 0; r < ROWS; ++r) {
            BB bit = BB(1) << (c + r * COLS);
            int cell_weight = CENTER_COL_WEIGHT[c] * CENTER_ROW_WEIGHT[r];
            if (cur & bit) score += cell_weight;
            if (opp & bit) score -= cell_weight;
        }
    }

    // ── 3. Fork bonus ─────────────────────────
    int cur_threats = count_threats(cur, opp);
    int opp_threats = count_threats(opp, cur);

    if (cur_threats >= 2) score += 200;  // fork — almost winning
    if (opp_threats >= 2) score -= 200;  // opponent fork — dangerous

    score += cur_threats * 15;
    score -= opp_threats * 15;

    return score;
}
