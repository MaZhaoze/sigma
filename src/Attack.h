#pragma once
#include <cstdint>

#include "types.h"
#include "Position.h"

namespace attacks {

using Bitboard = uint64_t;

// 返回单格 bitboard
constexpr inline Bitboard bb_sq(int sq) {
    return (sq >= 0 && sq < 64) ? (1ULL << sq) : 0ULL;
}

// 翻转颜色
constexpr inline Color flip(Color c) {
    return (c == WHITE) ? BLACK : WHITE;
}

// 预计算表：马 / 王 / 兵攻击
struct Tables {
    Bitboard knight[64]{};
    Bitboard king[64]{};
    Bitboard pawn[2][64]{};

    Tables();
};

// 获取全局攻击表
const Tables& T();

// 一些棋子类型判断小工具
constexpr inline bool is_slider_bishop_queen(Piece p, Color c) {
    return (c == WHITE) ? (p == W_BISHOP || p == W_QUEEN)
                        : (p == B_BISHOP || p == B_QUEEN);
}

constexpr inline bool is_slider_rook_queen(Piece p, Color c) {
    return (c == WHITE) ? (p == W_ROOK || p == W_QUEEN)
                        : (p == B_ROOK || p == B_QUEEN);
}

constexpr inline bool is_knight(Piece p, Color c) {
    return (c == WHITE) ? (p == W_KNIGHT) : (p == B_KNIGHT);
}

constexpr inline bool is_king(Piece p, Color c) {
    return (c == WHITE) ? (p == W_KING) : (p == B_KING);
}

constexpr inline bool is_pawn(Piece p, Color c) {
    return (c == WHITE) ? (p == W_PAWN) : (p == B_PAWN);
}

// 返回“哪些 byColor 的棋子正在攻击 sq”构成的 bitboard
Bitboard attackers_to_bb(const Position& pos, int sq, Color byColor);

// 返回攻击者数量
int attackers_to_count(const Position& pos, int sq, Color byColor);

// 判断某格是否被某方攻击
bool is_square_attacked(const Position& pos, int sq, Color byColor);

// 判断某方是否被将军
bool in_check(const Position& pos, Color sideToCheck);

} // namespace attacks