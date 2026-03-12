#include "MoveGeneration.h"

#include <array>
#include <vector>

namespace movegen {

namespace {

// =====================
// 预处理表
// 目的：
// 1. 马 / 王：直接查每个格子的合法目标格，避免运行时反复算 abs(file/rank)
// 2. 滑子：预处理每个格子在 8 个方向上的 ray，避免运行时做 same_rank / diag_step_ok
//
// 注意：
// 这里尽量保持“旧版 movegen 的生成顺序”不变，
// 因为 alpha-beta 对步序非常敏感。
// =====================

constexpr int DIR_N  = 0; // +8
constexpr int DIR_S  = 1; // -8
constexpr int DIR_E  = 2; // +1
constexpr int DIR_W  = 3; // -1
constexpr int DIR_NE = 4; // +9
constexpr int DIR_NW = 5; // +7
constexpr int DIR_SE = 6; // -7
constexpr int DIR_SW = 7; // -9

struct Tables {
    std::array<std::array<int, 8>, 64> knightMoves{};
    std::array<int, 64> knightCnt{};

    std::array<std::array<int, 8>, 64> kingMoves{};
    std::array<int, 64> kingCnt{};

    std::array<std::array<std::array<int, 7>, 8>, 64> rays{};
    std::array<std::array<int, 8>, 64> rayCnt{};
};

inline bool in_bounds(int f, int r) {
    return f >= 0 && f < 8 && r >= 0 && r < 8;
}

inline int make_sq_local(int f, int r) {
    return r * 8 + f;
}

// 用函数内静态对象，保证初始化线程安全
const Tables& tables() {
    static const Tables tbl = [] {
        Tables t{};

        // --------
        // 马
        // 尽量保持旧版偏移顺序：
        // {+17, +15, +10, +6, -6, -10, -15, -17}
        // --------
        static const int KNIGHT_DF[8] = {+1, -1, +2, -2, +2, -2, -1, +1};
        static const int KNIGHT_DR[8] = {+2, +2, +1, +1, -1, -1, -2, -2};

        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);

            int cnt = 0;
            for (int k = 0; k < 8; ++k) {
                int nf = f + KNIGHT_DF[k];
                int nr = r + KNIGHT_DR[k];
                if (!in_bounds(nf, nr))
                    continue;
                t.knightMoves[sq][cnt++] = make_sq_local(nf, nr);
            }
            t.knightCnt[sq] = cnt;
        }

        // --------
        // 王
        // 尽量保持旧版偏移顺序：
        // {+8, -8, +1, -1, +9, +7, -7, -9}
        // --------
        static const int KING_DF[8] = {0, 0, +1, -1, +1, -1, +1, -1};
        static const int KING_DR[8] = {+1, -1, 0, 0, +1, +1, -1, -1};

        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);

            int cnt = 0;
            for (int k = 0; k < 8; ++k) {
                int nf = f + KING_DF[k];
                int nr = r + KING_DR[k];
                if (!in_bounds(nf, nr))
                    continue;
                t.kingMoves[sq][cnt++] = make_sq_local(nf, nr);
            }
            t.kingCnt[sq] = cnt;
        }

        // --------
        // 滑子 ray
        // --------
        static const int RAY_DF[8] = {0, 0, +1, -1, +1, -1, +1, -1};
        static const int RAY_DR[8] = {+1, -1, 0, 0, +1, +1, -1, -1};

        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);

            for (int d = 0; d < 8; ++d) {
                int nf = f + RAY_DF[d];
                int nr = r + RAY_DR[d];

                int cnt = 0;
                while (in_bounds(nf, nr)) {
                    t.rays[sq][d][cnt++] = make_sq_local(nf, nr);
                    nf += RAY_DF[d];
                    nr += RAY_DR[d];
                }
                t.rayCnt[sq][d] = cnt;
            }
        }

        return t;
    }();

    return tbl;
}

// =====================
// 快路径加步函数
// =====================
inline void push_quiet(std::vector<Move>& moves, int from, int to) {
    moves.push_back(make_move(from, to, 0, 0));
}

inline void push_capture(std::vector<Move>& moves, int from, int to) {
    moves.push_back(make_move(from, to, MF_CAPTURE, 0));
}

inline void push_ep(std::vector<Move>& moves, int from, int to) {
    moves.push_back(make_move(from, to, MF_EP, 0));
}

inline void push_castle(std::vector<Move>& moves, int from, int to) {
    moves.push_back(make_move(from, to, MF_CASTLE, 0));
}

inline void push_promo_quiet(std::vector<Move>& moves, int from, int to) {
    moves.push_back(make_move(from, to, MF_PROMO, 1));
    moves.push_back(make_move(from, to, MF_PROMO, 2));
    moves.push_back(make_move(from, to, MF_PROMO, 3));
    moves.push_back(make_move(from, to, MF_PROMO, 4));
}

inline void push_promo_capture(std::vector<Move>& moves, int from, int to) {
    moves.push_back(make_move(from, to, MF_PROMO | MF_CAPTURE, 1));
    moves.push_back(make_move(from, to, MF_PROMO | MF_CAPTURE, 2));
    moves.push_back(make_move(from, to, MF_PROMO | MF_CAPTURE, 3));
    moves.push_back(make_move(from, to, MF_PROMO | MF_CAPTURE, 4));
}

inline void push_if_ok(const Position& pos, std::vector<Move>& moves, int from, int to) {
    Piece dst = pos.board[to];
    if (dst == NO_PIECE) {
        push_quiet(moves, from, to);
    } else if (!same_color(dst, pos.side)) {
        push_capture(moves, from, to);
    }
}

inline void gen_slider_dir(const Position& pos, std::vector<Move>& moves, int from, int dir) {
    const Tables& T = tables();
    const int cnt = T.rayCnt[from][dir];

    for (int i = 0; i < cnt; ++i) {
        int to = T.rays[from][dir][i];
        Piece dst = pos.board[to];

        if (dst == NO_PIECE) {
            push_quiet(moves, from, to);
            continue;
        }

        if (!same_color(dst, pos.side))
            push_capture(moves, from, to);

        break;
    }
}

} // namespace

// =====================
// 伪合法着法生成
// 关键：恢复为“按棋盘格顺序扫描”
// 这样最贴近旧版的全局出步顺序
// =====================
void generate_pseudo_legal(const Position& pos, std::vector<Move>& moves) {
    const Tables& T = tables();

    moves.clear();
    if (moves.capacity() < 256)
        moves.reserve(256);

    const Color us = pos.side;

    for (int sq = 0; sq < 64; ++sq) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;
        if (color_of(p) != us)
            continue;

        const PieceType pt = type_of(p);

        // =====================
        // 兵
        // =====================
        if (pt == PAWN) {
            const int r = rank_of(sq);
            const int f = file_of(sq);

            const int dir = (us == WHITE) ? +8 : -8;
            const int startRank = (us == WHITE) ? 1 : 6;
            const int promoFromRank = (us == WHITE) ? 6 : 1;

            const int one = sq + dir;
            if ((unsigned)one < 64u && pos.board[one] == NO_PIECE) {
                if (r == promoFromRank)
                    push_promo_quiet(moves, sq, one);
                else
                    push_quiet(moves, sq, one);

                if (r == startRank) {
                    const int two = sq + dir * 2;
                    if ((unsigned)two < 64u && pos.board[two] == NO_PIECE)
                        push_quiet(moves, sq, two);
                }
            }

            const int capL = (us == WHITE) ? (sq + 7) : (sq - 9);
            const int capR = (us == WHITE) ? (sq + 9) : (sq - 7);

            if (f != 0 && (unsigned)capL < 64u && enemy_color(pos.board[capL], us)) {
                if (r == promoFromRank)
                    push_promo_capture(moves, sq, capL);
                else
                    push_capture(moves, sq, capL);
            }

            if (f != 7 && (unsigned)capR < 64u && enemy_color(pos.board[capR], us)) {
                if (r == promoFromRank)
                    push_promo_capture(moves, sq, capR);
                else
                    push_capture(moves, sq, capR);
            }

            if (pos.epSquare != -1) {
                if (f != 0 && capL == pos.epSquare)
                    push_ep(moves, sq, capL);
                if (f != 7 && capR == pos.epSquare)
                    push_ep(moves, sq, capR);
            }

            continue;
        }

        // =====================
        // 马
        // =====================
        if (pt == KNIGHT) {
            const int cnt = T.knightCnt[sq];
            for (int i = 0; i < cnt; ++i) {
                const int to = T.knightMoves[sq][i];
                push_if_ok(pos, moves, sq, to);
            }
            continue;
        }

        // =====================
        // 王
        // =====================
        if (pt == KING) {
            const int cnt = T.kingCnt[sq];
            for (int i = 0; i < cnt; ++i) {
                const int to = T.kingMoves[sq][i];
                push_if_ok(pos, moves, sq, to);
            }

            if (us == WHITE && sq == E1) {
                if ((pos.castlingRights & CR_WK) &&
                    pos.board[F1] == NO_PIECE &&
                    pos.board[G1] == NO_PIECE) {
                    push_castle(moves, E1, G1);
                }

                if ((pos.castlingRights & CR_WQ) &&
                    pos.board[D1] == NO_PIECE &&
                    pos.board[C1] == NO_PIECE &&
                    pos.board[B1] == NO_PIECE) {
                    push_castle(moves, E1, C1);
                }
            } else if (us == BLACK && sq == E8) {
                if ((pos.castlingRights & CR_BK) &&
                    pos.board[F8] == NO_PIECE &&
                    pos.board[G8] == NO_PIECE) {
                    push_castle(moves, E8, G8);
                }

                if ((pos.castlingRights & CR_BQ) &&
                    pos.board[D8] == NO_PIECE &&
                    pos.board[C8] == NO_PIECE &&
                    pos.board[B8] == NO_PIECE) {
                    push_castle(moves, E8, C8);
                }
            }

            continue;
        }

        // =====================
        // 象 / 车 / 后
        // 顺序保持旧版
        // =====================
        if (pt == BISHOP) {
            gen_slider_dir(pos, moves, sq, DIR_NE);
            gen_slider_dir(pos, moves, sq, DIR_NW);
            gen_slider_dir(pos, moves, sq, DIR_SE);
            gen_slider_dir(pos, moves, sq, DIR_SW);
        } else if (pt == ROOK) {
            gen_slider_dir(pos, moves, sq, DIR_N);
            gen_slider_dir(pos, moves, sq, DIR_S);
            gen_slider_dir(pos, moves, sq, DIR_E);
            gen_slider_dir(pos, moves, sq, DIR_W);
        } else if (pt == QUEEN) {
            gen_slider_dir(pos, moves, sq, DIR_NE);
            gen_slider_dir(pos, moves, sq, DIR_NW);
            gen_slider_dir(pos, moves, sq, DIR_SE);
            gen_slider_dir(pos, moves, sq, DIR_SW);
            gen_slider_dir(pos, moves, sq, DIR_N);
            gen_slider_dir(pos, moves, sq, DIR_S);
            gen_slider_dir(pos, moves, sq, DIR_E);
            gen_slider_dir(pos, moves, sq, DIR_W);
        }
    }
}

// =====================
// 王车易位路径是否合法
// =====================
bool legal_castle_path_ok(const Position& pos, Move m) {
    int from = from_sq(m);
    int to = to_sq(m);

    Color us = pos.side;
    Color them = ~us;

    if (attacks::in_check(pos, us))
        return false;

    if (us == WHITE) {
        if (from == E1 && to == G1) {
            if (attacks::is_square_attacked(pos, F1, them))
                return false;
            if (attacks::is_square_attacked(pos, G1, them))
                return false;
            return true;
        }

        if (from == E1 && to == C1) {
            if (attacks::is_square_attacked(pos, D1, them))
                return false;
            if (attacks::is_square_attacked(pos, C1, them))
                return false;
            return true;
        }
    } else {
        if (from == E8 && to == G8) {
            if (attacks::is_square_attacked(pos, F8, them))
                return false;
            if (attacks::is_square_attacked(pos, G8, them))
                return false;
            return true;
        }

        if (from == E8 && to == C8) {
            if (attacks::is_square_attacked(pos, D8, them))
                return false;
            if (attacks::is_square_attacked(pos, C8, them))
                return false;
            return true;
        }
    }

    return true;
}

// =====================
// 合法着法生成
// =====================
void generate_legal(Position& pos, std::vector<Move>& legal) {
    static thread_local std::vector<Move> pseudo;

    generate_pseudo_legal(pos, pseudo);

    legal.clear();
    legal.reserve(pseudo.size());

    Color us = pos.side;

    for (Move m : pseudo) {
        if (flags_of(m) & MF_CASTLE) {
            if (!legal_castle_path_ok(pos, m))
                continue;
        }

        Undo u = pos.do_move(m);
        bool ok = !attacks::in_check(pos, us);
        pos.undo_move(m, u);

        if (ok)
            legal.push_back(m);
    }
}

// =====================
// 合法吃子生成
// =====================
void generate_legal_captures(Position& pos, std::vector<Move>& caps) {
    std::vector<Move> legal;
    legal.reserve(256);

    generate_legal(pos, legal);

    caps.clear();
    caps.reserve(legal.size());

    for (Move m : legal) {
        if ((flags_of(m) & MF_CAPTURE) ||
            (flags_of(m) & MF_EP) ||
            promo_of(m)) {
            caps.push_back(m);
        }
    }
}

} // namespace movegen