#pragma once
#include <cstdint>

#include "types.h"
#include "Position.h"

// Static exchange evaluation (SEE) helpers.

using U64 = uint64_t;

// 小工具函数：保留 inline 即可
inline U64 bb_sq(int sq) {
    return (U64(1) << sq);
}

inline int pop_lsb(U64& b) {
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, b);
    b &= b - 1;
    return (int)idx;
#else
    int idx = __builtin_ctzll(b);
    b &= b - 1;
    return idx;
#endif
}

inline int piece_value_pt(PieceType pt) {
    switch (pt) {
    case PAWN:   return 100;
    case KNIGHT: return 320;
    case BISHOP: return 330;
    case ROOK:   return 500;
    case QUEEN:  return 900;
    case KING:   return 20000;
    default:     return 0;
    }
}

inline int piece_value(Piece p) {
    return piece_value_pt(type_of(p));
}

// 升变编码 -> PieceType
PieceType promo_to_pt(int promoCode);

// 从棋盘数组生成 occupancy bitboard
U64 occ_from_board(const Piece board[64]);

// 基础辅助
bool on_board(int sq);

// 攻击者收集
void add_pawn_attackers_to(U64& attackers, const Piece b[64], int toSq);
void add_knight_attackers_to(U64& attackers, const Piece b[64], int toSq);
void add_king_attackers_to(U64& attackers, const Piece b[64], int toSq);
void ray_first_attacker(U64& attackers, const Piece b[64], int toSq, int df, int dr, bool diag);

U64 attackers_to_sq(const Piece b[64], int toSq);
U64 color_attackers(U64 attackers, const Piece b[64], Color c);

// 找某一方的最便宜攻击者
int least_valuable_attacker_sq(U64 attackersSide, const Piece b[64]);

// 判断某个兵走法是否是“吃子升变”
bool pawn_promo_by_move(Color side, int fromSq, int toSq);

// 完整 SEE
int see_full(const Position& pos, Move m);

// 阈值判断
bool see_ge(const Position& pos, Move m, int threshold);