#pragma once
#include <cstdint>
#include <string>

#include "types.h"

// Position representation and move make/undo with incremental Zobrist.

// =====================
// Bitboard 基础
// 说明：
// 1. 先与现有 mailbox(board[64]) 双轨共存
// 2. 第一阶段先支持“从 board 重建 bitboard”
// 3. 后续再把 attacks / movegen / do_move 增量维护逐步接上
// =====================
using Bitboard = uint64_t;

// 不依赖 types.h 里的数量常量，自己定义最小可用版本
static constexpr int BB_COLOR_NB = 2;      // WHITE / BLACK
static constexpr int BB_PTYPE_NB = 7;      // NONE..KING（直接用 PieceType 当下标）

inline constexpr Bitboard sq_bb(int sq) {
    return 1ULL << sq;
}

inline int bb_popcount(Bitboard b) {
    return __builtin_popcountll(b);
}

inline int bb_lsb(Bitboard b) {
    return __builtin_ctzll(b);
}

inline int bb_pop_lsb(Bitboard& b) {
    int s = bb_lsb(b);
    b &= (b - 1);
    return s;
}

// 王车易位权 bitmask
enum CastlingRight : int {
    CR_NONE = 0,
    CR_WK = 1 << 0, // 白王短易位
    CR_WQ = 1 << 1, // 白王长易位
    CR_BK = 1 << 2, // 黑王短易位
    CR_BQ = 1 << 3  // 黑王长易位
};

// 撤销一步所需的状态快照
struct Undo {
    Piece moved = NO_PIECE;
    Piece captured = NO_PIECE;

    Color prevSide = WHITE;

    int prevCastling = CR_NONE;
    int prevEpSquare = -1;
    int prevHalfmove = 0;
    int prevFullmove = 1;

    uint64_t prevKey = 0; // 走子前的 zobrist

    // 吃过路兵时，被吃兵所在格
    int epCapturedSq = -1;

    // 王车易位时车的移动
    int rookFrom = -1;
    int rookTo = -1;
};

// 棋盘局面
struct Position {
    // =====================
    // 传统 mailbox 表示
    // 先保留，保证现有逻辑兼容
    // =====================
    Piece board[64];
    Color side = WHITE;

    int castlingRights = CR_WK | CR_WQ | CR_BK | CR_BQ;
    int epSquare = -1; // 0..63 or -1
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    uint64_t zobKey = 0;

    // =====================
    // Bitboard 表示
    // 约定：
    // pieces[color][pieceType]
    // color: WHITE / BLACK
    // pieceType: NONE..KING
    //
    // 其中 pieces[*][NONE] 预留不用，
    // 后续可以直接用 type_of(p) 当数组下标，最省事。
    // =====================
    Bitboard pieces[BB_COLOR_NB][BB_PTYPE_NB]{};
    Bitboard occ[BB_COLOR_NB]{};
    Bitboard occAll = 0ULL;

    // 王所在格，便于 in_check / attacks 快速使用
    int kingSq[BB_COLOR_NB]{-1, -1};

    Position();

    // 基础操作
    void clear();
    void set_startpos();
    void set_fen(const std::string& fen);

    // 工具函数
    static Piece char_to_piece(char c);
    static int algebraic_to_sq(const std::string& s);
    static std::string sq_to_algebraic(int sq);

    // Zobrist
    void recompute_zobrist();
    void apply_zobrist_delta_after_move(const Undo& u, Move m);

    // 王车易位权维护
    void remove_castling_for_king(Color c);
    void remove_castling_for_rook_square(int sq);

    // 走子 / 悔棋
    Undo do_move(Move m);
    void undo_move(Move m, const Undo& u);

    // =====================
    // Bitboard 维护 / 调试
    // =====================

    // 从当前 board[64] 全量重建 bitboard 状态
    // 第一阶段优先保证正确性，再考虑 do_move/undo_move 增量维护
    void rebuild_bitboards();

    // 验证 board[64] 与 bitboard 状态是否一致
    // 调试阶段非常有用
    bool verify_bitboards() const;

    // 调试辅助
    int king_square(Color c) const;
};