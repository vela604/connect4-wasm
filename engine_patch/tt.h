#pragma once
#include "bitboard.h"
#include <vector>
#include <random>
#include <atomic>
#include <cstring>

// ─────────────────────────────────────────────
//  Zobrist Hashing
// ─────────────────────────────────────────────
struct ZobristTable {
    // [player 0/1][col 0-6][row 0-5]
    uint64_t table[2][COLS][ROWS];

    ZobristTable() {
        std::mt19937_64 rng(0xDEADBEEFCAFEBABEULL);
        for (auto& p : table)
            for (auto& c : p)
                for (auto& r : c)
                    r = rng();
    }

    uint64_t hash(const Position& pos) const {
        uint64_t h = 0;
        for (int c = 0; c < COLS; ++c) {
            for (int r = 0; r < ROWS; ++r) {
                BB bit = BB(1) << (c + r * COLS);
                if (pos.mask & bit) {
                    int player = (pos.current & bit) ? pos.side_to_move() : (1 - pos.side_to_move());
                    h ^= table[player][c][r];
                }
            }
        }
        return h;
    }
};

// Global Zobrist table
inline ZobristTable ZOBRIST;

// ─────────────────────────────────────────────
//  Transposition Table Entry
//
//  Plain value type used to pass results out of
//  the table. Not used for internal storage
//  (internal storage is lock-free/atomic — see
//  below) so it is always safe to copy across
//  threads.
// ─────────────────────────────────────────────
enum class TTFlag : uint8_t {
    EXACT = 0,   // exact score
    LOWER = 1,   // alpha bound (score >= value)
    UPPER = 2,   // beta  bound (score <= value)
    EMPTY = 3
};

struct TTEntry {
    int32_t  score;
    int8_t   depth;
    int8_t   best_move;  // column 0-6, or -1
    TTFlag   flag;

    TTEntry() : score(0), depth(-1), best_move(-1), flag(TTFlag::EMPTY) {}
};

// ─────────────────────────────────────────────
//  Transposition Table — lock-free, thread-safe
//
//  Multiple search threads (one per CPU core,
//  see Engine's auto thread-count detection)
//  probe/store this table concurrently. Instead
//  of a mutex per access (slow, kills scaling
//  across many cores — phones and PCs alike),
//  we use the classic "XOR trick" (as used in
//  Stockfish): each slot's 64-bit data word and
//  a (key ^ data) word are stored/read with
//  plain atomics. A torn read (one thread reads
//  mid-write from another thread) can only ever
//  produce a key mismatch — never a silently
//  corrupted-but-valid-looking entry — so no
//  locks are required for correctness.
// ─────────────────────────────────────────────
class TranspositionTable {
public:
    static constexpr size_t DEFAULT_SIZE_MB = 256;

    explicit TranspositionTable(size_t size_mb = DEFAULT_SIZE_MB) {
        resize(size_mb);
    }

    void resize(size_t size_mb) {
        size_t bytes   = size_mb * 1024 * 1024;
        num_entries_   = bytes / sizeof(Slot);
        if (num_entries_ == 0) num_entries_ = 1;
        // Round down to power of 2 for fast modulo
        size_t pow2 = 1;
        while (pow2 * 2 <= num_entries_) pow2 *= 2;
        num_entries_ = pow2;
        mask_        = num_entries_ - 1;
        table_       = std::vector<Slot>(num_entries_);
        clear();
    }

    void clear() {
        uint64_t empty_data = pack(0, -1, -1, TTFlag::EMPTY);
        for (auto& s : table_) {
            s.data.store(empty_data, std::memory_order_relaxed);
            s.key_xor_data.store(empty_data, std::memory_order_relaxed); // key=0 ^ empty_data
        }
        hits_.store(0,   std::memory_order_relaxed);
        misses_.store(0, std::memory_order_relaxed);
        stores_.store(0, std::memory_order_relaxed);
    }

    // Store entry — safe to call from any number of threads concurrently.
    void store(uint64_t key, int score, int depth, int best_move, TTFlag flag) {
        size_t  idx  = key & mask_;
        uint64_t data = pack(score, depth, best_move, flag);
        Slot&   slot = table_[idx];
        // Always-replace strategy (simple, effective, and avoids needing
        // read-modify-write — important for lock-free correctness).
        slot.data.store(data, std::memory_order_relaxed);
        slot.key_xor_data.store(key ^ data, std::memory_order_relaxed);
        stores_.fetch_add(1, std::memory_order_relaxed);
    }

    // Probe entry. Returns true and fills `out` on a verified hit.
    // Safe to call from any number of threads concurrently.
    bool probe(uint64_t key, TTEntry& out) const {
        size_t idx = key & mask_;
        const Slot& slot = table_[idx];

        uint64_t data = slot.data.load(std::memory_order_relaxed);
        uint64_t kxd  = slot.key_xor_data.load(std::memory_order_relaxed);

        // Reconstruct the key this (data, kxd) pair was written for.
        // If another thread is mid-write, data/kxd may belong to two
        // different writes — in that case the reconstructed key will
        // (with overwhelming probability) not match `key`, and we
        // correctly treat it as a miss rather than risk corruption.
        if ((kxd ^ data) != key) {
            misses_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        int32_t score; int8_t depth, best_move; TTFlag flag;
        unpack(data, score, depth, best_move, flag);
        if (depth < 0) { // empty slot sentinel
            misses_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        out.score     = score;
        out.depth     = depth;
        out.best_move = best_move;
        out.flag      = flag;
        hits_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Stats
    uint64_t hits()   const { return hits_.load(std::memory_order_relaxed);   }
    uint64_t misses() const { return misses_.load(std::memory_order_relaxed); }
    uint64_t stores() const { return stores_.load(std::memory_order_relaxed); }
    double   usage()  const {
        size_t used = 0;
        for (auto& s : table_) {
            uint64_t data = s.data.load(std::memory_order_relaxed);
            int32_t score; int8_t depth, best_move; TTFlag flag;
            unpack(data, score, depth, best_move, flag);
            if (depth >= 0) ++used;
        }
        return 100.0 * used / num_entries_;
    }
    size_t   size()   const { return num_entries_; }

private:
    struct Slot {
        std::atomic<uint64_t> data{0};
        std::atomic<uint64_t> key_xor_data{0};
    };

    static uint64_t pack(int32_t score, int8_t depth, int8_t best_move, TTFlag flag) {
        uint64_t d = 0;
        d |= (uint64_t)(uint32_t)score;
        d |= (uint64_t)(uint8_t)depth     << 32;
        d |= (uint64_t)(uint8_t)best_move << 40;
        d |= (uint64_t)(uint8_t)flag      << 48;
        return d;
    }

    static void unpack(uint64_t d, int32_t& score, int8_t& depth, int8_t& best_move, TTFlag& flag) {
        score     = (int32_t)(uint32_t)(d & 0xFFFFFFFFull);
        depth     = (int8_t)((d >> 32) & 0xFF);
        best_move = (int8_t)((d >> 40) & 0xFF);
        flag      = (TTFlag)((d >> 48) & 0xFF);
    }

    std::vector<Slot>         table_;
    size_t                    num_entries_ = 0;
    size_t                    mask_        = 0;
    mutable std::atomic<uint64_t> hits_{0};
    mutable std::atomic<uint64_t> misses_{0};
    std::atomic<uint64_t>         stores_{0};
};
