#pragma once
#include <cstdint>

using Bitboard = uint64_t;

constexpr Bitboard BB_EMPTY = 0ULL;

inline constexpr Bitboard sq_bb(int sq) {
    return 1ULL << sq;
}

inline int popcount(Bitboard b) {
    return __builtin_popcountll(b);
}

inline int lsb(Bitboard b) {
    return __builtin_ctzll(b);
}

inline int pop_lsb(Bitboard& b) {
    int s = lsb(b);
    b &= (b - 1);
    return s;
}