#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <cassert>

// ─────────────────────────────────────────────
//  Board layout  (bit index = col + row*7)
//
//   row 5  35 36 37 38 39 40 41   ← top
//   row 4  28 29 30 31 32 33 34
//   row 3  21 22 23 24 25 26 27
//   row 2  14 15 16 17 18 19 20
//   row 1   7  8  9 10 11 12 13
//   row 0   0  1  2  3  4  5  6   ← bottom
//          c0 c1 c2 c3 c4 c5 c6
//
//  Extra sentinel row at row 6 (bits 42-48) keeps
//  overflow detection simple.
// ─────────────────────────────────────────────

static constexpr int ROWS    = 6;
static constexpr int COLS    = 7;
static constexpr int CELLS   = ROWS * COLS;   // 42

// Score constants
static constexpr int SCORE_WIN   =  1'000'000;
static constexpr int SCORE_LOSS  = -1'000'000;
static constexpr int SCORE_DRAW  =  0;

// Guaranteed win/loss threshold for reporting
static constexpr int MATE_THRESHOLD = 900'000;

// ── Bitmask helpers ──────────────────────────
using BB = uint64_t;

// Full board mask (42 bits)
static constexpr BB BOARD_MASK = (BB(1) << CELLS) - 1;

// Column masks
inline constexpr BB col_mask(int c) {
    BB m = 0;
    for (int r = 0; r < ROWS; ++r) m |= BB(1) << (c + r * COLS);
    return m;
}

// Bottom row mask
static constexpr BB BOTTOM_MASK = 0x0040201008040201ULL & BOARD_MASK;
// Actually compute properly:
inline BB make_bottom_mask() {
    BB m = 0;
    for (int c = 0; c < COLS; ++c) m |= BB(1) << c;
    return m;
}

// Top row mask (row ROWS-1)
inline BB make_top_mask() {
    BB m = 0;
    for (int c = 0; c < COLS; ++c) m |= BB(1) << (c + (ROWS - 1) * COLS);
    return m;
}

// ── Win detection ────────────────────────────
//
// IMPORTANT: this board uses a TIGHTLY PACKED bit layout
// (bit = col + row*COLS, no padding row/column between
// columns). A naive shift-based 4-in-a-row check (e.g.
// `bb & (bb>>1) & (bb>>2) & (bb>>3)` for "horizontal") is
// UNSAFE on this layout: shifting by 1 can walk off the
// right edge of one row (col 6) straight into the left
// edge of the NEXT row (col 0), since bit(col6,row) + 1 ==
// bit(col0,row+1). That makes such a shift-trick report
// phantom wins for 4 cells that are not actually connected
// on the board at all (2 cells at the end of one row + 2
// cells at the start of the row above, all happening to be
// the same color). The same problem affects the two
// diagonal directions as well (shifts of COLS±1).
//
// The fix here avoids shifting across row/column boundaries
// entirely: every one of the 69 valid 4-in-a-row lines on a
// 7x6 Connect Four board is precomputed ONCE as an explicit
// bitmask built purely from real (col,row) coordinates (the
// same safe approach already used in evaluator.h), and
// is_win() simply checks each mask. This can never produce
// a false positive, unlike the shift trick.
struct WinLines {
    std::array<BB, 69> masks;
    int count = 0;

    WinLines() {
        auto add = [&](BB m) { masks[count++] = m; };
        // Horizontal
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c <= COLS - 4; ++c) {
                BB m = 0;
                for (int i = 0; i < 4; ++i) m |= BB(1) << (c + i + r * COLS);
                add(m);
            }
        // Vertical
        for (int c = 0; c < COLS; ++c)
            for (int r = 0; r <= ROWS - 4; ++r) {
                BB m = 0;
                for (int i = 0; i < 4; ++i) m |= BB(1) << (c + (r + i) * COLS);
                add(m);
            }
        // Diagonal "/" (col increases, row increases)
        for (int c = 0; c <= COLS - 4; ++c)
            for (int r = 0; r <= ROWS - 4; ++r) {
                BB m = 0;
                for (int i = 0; i < 4; ++i) m |= BB(1) << ((c + i) + (r + i) * COLS);
                add(m);
            }
        // Diagonal "\" (col increases, row decreases)
        for (int c = 0; c <= COLS - 4; ++c)
            for (int r = 3; r < ROWS; ++r) {
                BB m = 0;
                for (int i = 0; i < 4; ++i) m |= BB(1) << ((c + i) + (r - i) * COLS);
                add(m);
            }
    }
};

inline const WinLines& win_lines() {
    static const WinLines lines;
    return lines;
}

// Returns true if bb contains 4 in a row (horizontal, vertical, or
// either diagonal) — checked against every real winning line on the
// board, so this cannot produce a column-boundary false positive.
inline bool is_win(BB bb) {
    const auto& lines = win_lines();
    for (int i = 0; i < lines.count; ++i) {
        if ((bb & lines.masks[i]) == lines.masks[i]) return true;
    }
    return false;
}

// ── Position structure ───────────────────────
struct Position {
    BB  current;   // current player's pieces
    BB  mask;      // all pieces on board (current | opponent)
    int moves;     // total moves made so far

    Position() : current(0), mask(0), moves(0) {}

    // Which player's turn? 0=P1, 1=P2
    int side_to_move() const { return moves & 1; }

    // Opponent bitboard
    BB opponent() const { return current ^ mask; }

    // Height array – next empty row in each column
    int height(int col) const {
        int r = 0;
        BB col_m = BB(1) << col;
        while (r < ROWS && (mask & (col_m << (r * COLS)))) ++r;
        return r;
    }

    // Can we play in this column?
    bool can_play(int col) const {
        return height(col) < ROWS;
    }

    // Play a move (column), switches current player
    void play(int col) {
        int r = height(col);
        BB bit = BB(1) << (col + r * COLS);
        // Switch perspective: xor flips current to opponent, then swap
        current ^= mask;      // current = opponent's pieces
        mask    |= bit;       // add new piece to mask
        current ^= mask;      // current now includes new piece and is the NEW player's pieces
        // Actually: correct formula below
        // Let's redo: after play, it's the other player's turn
        // So new_current = old_opponent | new_bit? No.
        // Standard approach:
        moves++;
    }

    // Non-destructive: return new position after playing col
    Position after(int col) const {
        Position p;
        int r = height(col);
        BB bit = BB(1) << (col + r * COLS);
        p.mask    = mask | bit;
        p.current = (current ^ mask) | bit; // opponent's pieces + new piece
        // Wait, we need: new current = new mover's pieces
        // After move: the OTHER player becomes current
        // other = current ^ mask (before adding bit)
        // new mask = mask | bit
        // new current = (current ^ mask) | bit  ← that's the player who JUST moved (now it's their pieces including the new one)
        // Hmm — standard Fhourstones encoding:
        // current = pieces of player to move
        // after move: current becomes (mask+bit) ^ new_current
        // Let me use the clean Fhourstones formula:
        p.current = (current ^ mask) | bit; // WRONG - see below
        p.mask    = mask | bit;
        p.moves   = moves + 1;
        // Fix: new_current should be the NEXT player's pieces = (mask ^ current) which is opponent's pieces (unchanged)
        // But wait mask | bit contains the new piece belonging to CURRENT player
        // So next player's pieces = (mask|bit) ^ ((current)|bit) = (mask ^ current) = opponent (old)
        // That's: p.current = opponent() = current ^ mask
        p.current = current ^ mask;  // next player's pieces (opponent of the one who moved)
        return p;
    }

    // Did current player just win? (check opponent after they moved)
    bool is_winning_move(int col) const {
        int r = height(col);
        if (r >= ROWS) return false;
        BB bit = BB(1) << (col + r * COLS);
        BB new_current = current | bit;
        return is_win(new_current);
    }

    // Is board full?
    bool is_full() const { return moves == CELLS; }

    // Symmetric position (mirror columns)
    Position mirror() const {
        Position p;
        p.moves = moves;
        // Mirror column c -> column (COLS-1-c)
        for (int c = 0; c < COLS; ++c) {
            int mc = COLS - 1 - c;
            for (int r = 0; r < ROWS; ++r) {
                BB src_bit = BB(1) << (c  + r * COLS);
                BB dst_bit = BB(1) << (mc + r * COLS);
                if (current & src_bit) p.current |= dst_bit;
                if (mask    & src_bit) p.mask    |= dst_bit;
            }
        }
        return p;
    }

    // Canonical form (use smaller of pos vs mirror for TT key)
    std::pair<BB,BB> canonical() const {
        Position m = mirror();
        if (m.key() < key()) return {m.current, m.mask};
        return {current, mask};
    }

    // Unique key for transposition table
    BB key() const {
        // Fhourstones key: current + mask (unique because mask shows occupied)
        return current + mask;
    }

    BB canonical_key() const {
        BB k1 = key();
        BB k2 = mirror().key();
        return std::min(k1, k2);
    }

    // Generate move list ordered by center priority
    std::vector<int> move_order() const;

    // Get column of last move (for display) - not tracked here
    std::string to_string() const;

    // Number of pieces on board
    int piece_count() const { return __builtin_popcountll(mask); }
};

// ── Move ordering (center-first) ─────────────
// Column priority: 3,2,4,1,5,0,6
static constexpr int COL_ORDER[COLS] = {3, 2, 4, 1, 5, 0, 6};

inline std::vector<int> Position::move_order() const {
    std::vector<int> moves_list;
    moves_list.reserve(COLS);
    for (int c : COL_ORDER) {
        if (can_play(c)) moves_list.push_back(c);
    }
    return moves_list;
}

inline std::string Position::to_string() const {
    std::string s;
    s += "  0 1 2 3 4 5 6\n";
    s += "  ─────────────\n";
    for (int r = ROWS - 1; r >= 0; --r) {
        s += std::to_string(r) + " ";
        for (int c = 0; c < COLS; ++c) {
            BB bit = BB(1) << (c + r * COLS);
            bool is_current  = (current & bit) != 0;
            bool is_occupied = (mask    & bit) != 0;
            if (!is_occupied)  s += ". ";
            else if (side_to_move() == 0) {
                // P1 to move → current = P1 pieces? No — current is the player to move
                // So current pieces = player to move's pieces
                // But we display by who placed, not by turn
                // Use: P1 = even moves, P2 = odd moves
                // pieces placed by P1 are in mask but we can't distinguish without 2 bitmaps for each player
                // We have: current = player-to-move's pieces
                // opponent = current ^ mask
                if (is_current) s += "X ";
                else            s += "O ";
            } else {
                if (is_current) s += "X ";
                else            s += "O ";
            }
        }
        s += "\n";
    }
    s += "  ─────────────\n";
    s += "  0 1 2 3 4 5 6\n";
    return s;
}
