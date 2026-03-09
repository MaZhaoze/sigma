#pragma once
#include <cstdint>
#include <string>

#include "types.h"

// Position representation and move make/undo with incremental Zobrist.

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
    Piece board[64];
    Color side = WHITE;

    int castlingRights = CR_WK | CR_WQ | CR_BK | CR_BQ;
    int epSquare = -1; // 0..63 or -1
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    uint64_t zobKey = 0;

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

    // 调试辅助
    int king_square(Color c) const;
};