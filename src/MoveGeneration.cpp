#include "MoveGeneration.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace movegen {

// =====================
// 基础加步函数
// 功能：
// 1. 检查目标格是否在棋盘内
// 2. 不能吃自己人
// 3. 如果目标格有子，则自动打上 MF_CAPTURE
// 4. 最后把走法压进 moves
// =====================
void push_move(const Position& pos, std::vector<Move>& moves, int from, int to, int flags, int promo) {
    if (!on_board(to))
        return;

    Piece dst = pos.board[to];

    // 不能走到己方棋子所在格
    if (same_color(dst, pos.side))
        return;

    // 如果目标格有棋子，自动补 capture 标记
    if (dst != NO_PIECE)
        flags |= MF_CAPTURE;

    moves.push_back(make_move(from, to, flags, promo));
}

// 加入四种升变
// promo 编码约定：1=N 2=B 3=R 4=Q
void add_promo(const Position& pos, std::vector<Move>& moves, int from, int to, int flags) {
    push_move(pos, moves, from, to, flags | MF_PROMO, 1);
    push_move(pos, moves, from, to, flags | MF_PROMO, 2);
    push_move(pos, moves, from, to, flags | MF_PROMO, 3);
    push_move(pos, moves, from, to, flags | MF_PROMO, 4);
}

// =====================
// 伪合法着法生成
// “伪合法”表示：
// - 走法本身符合棋子移动规则
// - 但不保证走完之后自己王不在被将军状态
//
// 例如：被钉住的棋子乱走，可能仍会被这里生成出来
// 真正过滤这些非法步，要靠 generate_legal
// =====================
void generate_pseudo_legal(const Position& pos, std::vector<Move>& moves) {
    moves.clear();
    if (moves.capacity() < 256)
        moves.reserve(256);

    Color us = pos.side;

    // 马和王的偏移
    static const int N_OFF[8] = {+17, +15, +10, +6, -6, -10, -15, -17};
    static const int K_OFF[8] = {+8, -8, +1, -1, +9, +7, -7, -9};

    // 象 / 车滑动方向
    static const int DIR_B[4] = {+9, +7, -7, -9};
    static const int DIR_R[4] = {+8, -8, +1, -1};

    // 枚举棋盘上所有格子，找到当前行棋方的所有棋子
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;
        if (color_of(p) != us)
            continue;

        PieceType pt = type_of(p);

        // =====================
        // 兵
        // =====================
        if (pt == PAWN) {
            const int dir = (us == WHITE) ? +8 : -8;
            const int startRank = (us == WHITE) ? 1 : 6;
            const int promoFromRank = (us == WHITE) ? 6 : 1;

            // --------
            // 单步前进
            // --------
            int one = sq + dir;
            if (on_board(one) && pos.board[one] == NO_PIECE) {
                // 如果兵已经在升变前一排，则前进一步要生成四种升变
                if (rank_of(sq) == promoFromRank)
                    add_promo(pos, moves, sq, one);
                else
                    push_move(pos, moves, sq, one);

                // --------
                // 双步前进
                // 只有初始排才允许，且中间和终点都必须为空
                // --------
                if (rank_of(sq) == startRank) {
                    int two = sq + dir * 2;
                    if (on_board(two) && pos.board[two] == NO_PIECE)
                        push_move(pos, moves, sq, two);
                }
            }

            // 兵斜吃
            int capL = (us == WHITE) ? (sq + 7) : (sq - 9);
            int capR = (us == WHITE) ? (sq + 9) : (sq - 7);

            // 左吃
            if (file_of(sq) != 0 && on_board(capL) && enemy_color(pos.board[capL], us)) {
                if (rank_of(sq) == promoFromRank)
                    add_promo(pos, moves, sq, capL);
                else
                    push_move(pos, moves, sq, capL);
            }

            // 右吃
            if (file_of(sq) != 7 && on_board(capR) && enemy_color(pos.board[capR], us)) {
                if (rank_of(sq) == promoFromRank)
                    add_promo(pos, moves, sq, capR);
                else
                    push_move(pos, moves, sq, capR);
            }

            // --------
            // 吃过路兵
            // 这里只做伪合法加入
            // 真正是否合法（是否暴露自己王）后面再统一过滤
            // --------
            if (pos.epSquare != -1) {
                if (file_of(sq) != 0 && capL == pos.epSquare) {
                    push_move(pos, moves, sq, capL, MF_EP);
                }
                if (file_of(sq) != 7 && capR == pos.epSquare) {
                    push_move(pos, moves, sq, capR, MF_EP);
                }
            }

            continue;
        }

        // =====================
        // 马
        // 用固定偏移枚举可能落点
        // 然后再用 (df, dr) 检查，防止横向绕边
        // =====================
        if (pt == KNIGHT) {
            int f = file_of(sq), r = rank_of(sq);

            for (int k = 0; k < 8; k++) {
                int to = sq + N_OFF[k];
                if (!on_board(to))
                    continue;

                int tf = file_of(to), tr = rank_of(to);
                int df = std::abs(tf - f), dr = std::abs(tr - r);

                // 必须严格是“日”字
                if (!((df == 1 && dr == 2) || (df == 2 && dr == 1)))
                    continue;

                push_move(pos, moves, sq, to);
            }

            continue;
        }

        // =====================
        // 王
        // 普通王步 + 王车易位（伪合法）
        // =====================
        if (pt == KING) {
            int f = file_of(sq), r = rank_of(sq);

            // 普通一步
            for (int k = 0; k < 8; k++) {
                int to = sq + K_OFF[k];
                if (!on_board(to))
                    continue;

                int tf = file_of(to), tr = rank_of(to);
                if (std::abs(tf - f) > 1 || std::abs(tr - r) > 1)
                    continue;

                push_move(pos, moves, sq, to);
            }

            // --------
            // 王车易位（这里只检查：
            // 1. 易位权是否存在
            // 2. 路径是否为空
            //
            // “王是否经过或走到被攻击格”不在这里判断，
            // 后面 legal_castle_path_ok 再做。
            // --------
            if (us == WHITE && sq == E1) {
                // 白王短易位
                if ((pos.castlingRights & CR_WK) &&
                    pos.board[F1] == NO_PIECE &&
                    pos.board[G1] == NO_PIECE) {
                    push_move(pos, moves, E1, G1, MF_CASTLE, 0);
                }

                // 白王长易位
                if ((pos.castlingRights & CR_WQ) &&
                    pos.board[D1] == NO_PIECE &&
                    pos.board[C1] == NO_PIECE &&
                    pos.board[B1] == NO_PIECE) {
                    push_move(pos, moves, E1, C1, MF_CASTLE, 0);
                }
            } else if (us == BLACK && sq == E8) {
                // 黑王短易位
                if ((pos.castlingRights & CR_BK) &&
                    pos.board[F8] == NO_PIECE &&
                    pos.board[G8] == NO_PIECE) {
                    push_move(pos, moves, E8, G8, MF_CASTLE, 0);
                }

                // 黑王长易位
                if ((pos.castlingRights & CR_BQ) &&
                    pos.board[D8] == NO_PIECE &&
                    pos.board[C8] == NO_PIECE &&
                    pos.board[B8] == NO_PIECE) {
                    push_move(pos, moves, E8, C8, MF_CASTLE, 0);
                }
            }

            continue;
        }

        // =====================
        // 滑子：象 / 车 / 后
        // 统一用一个 slide lambda 处理
        // =====================
        auto slide = [&](const int* dirs, int cnt) {
            for (int i = 0; i < cnt; i++) {
                int d = dirs[i];
                int cur = sq;

                while (true) {
                    int to = cur + d;
                    if (!on_board(to))
                        break;

                    // 左右平移时不能串到下一排
                    if ((d == +1 || d == -1) && !same_rank(cur, to))
                        break;

                    // 对角移动时必须保证 file 恰好变化 1
                    // 防止从边缘“绕过去”
                    if ((d == +9 || d == -9 || d == +7 || d == -7) && !diag_step_ok(cur, to))
                        break;

                    // 空格：可以继续滑
                    if (pos.board[to] == NO_PIECE) {
                        push_move(pos, moves, sq, to);
                        cur = to;
                        continue;
                    }

                    // 遇到敌子：可以吃，但不能再继续穿过去
                    if (enemy_color(pos.board[to], us))
                        push_move(pos, moves, sq, to);

                    break;
                }
            }
        };

        if (pt == BISHOP)
            slide(DIR_B, 4);
        else if (pt == ROOK)
            slide(DIR_R, 4);
        else if (pt == QUEEN) {
            slide(DIR_B, 4);
            slide(DIR_R, 4);
        }
    }
}

// =====================
// 王车易位路径是否合法
// 除了“有易位权 + 路径为空”之外，
// 真正合法的易位还必须满足：
// 1. 当前不能正在被将军
// 2. 王经过的格不能被攻击
// 3. 王落点不能被攻击
// =====================
bool legal_castle_path_ok(const Position& pos, Move m) {
    int from = from_sq(m);
    int to = to_sq(m);

    Color us = pos.side;
    Color them = ~us;

    // 如果当前就在被将军，就不能易位
    if (attacks::in_check(pos, us))
        return false;

    if (us == WHITE) {
        // 白短易位：E1 -> G1，需要检查 F1、G1
        if (from == E1 && to == G1) {
            if (attacks::is_square_attacked(pos, F1, them))
                return false;
            if (attacks::is_square_attacked(pos, G1, them))
                return false;
            return true;
        }

        // 白长易位：E1 -> C1，需要检查 D1、C1
        if (from == E1 && to == C1) {
            if (attacks::is_square_attacked(pos, D1, them))
                return false;
            if (attacks::is_square_attacked(pos, C1, them))
                return false;
            return true;
        }
    } else {
        // 黑短易位：E8 -> G8，需要检查 F8、G8
        if (from == E8 && to == G8) {
            if (attacks::is_square_attacked(pos, F8, them))
                return false;
            if (attacks::is_square_attacked(pos, G8, them))
                return false;
            return true;
        }

        // 黑长易位：E8 -> C8，需要检查 D8、C8
        if (from == E8 && to == C8) {
            if (attacks::is_square_attacked(pos, D8, them))
                return false;
            if (attacks::is_square_attacked(pos, C8, them))
                return false;
            return true;
        }
    }

    // 理论上正常易位只会落到上面几种情况
    // 这里保守返回 true，不额外拦截
    return true;
}

// =====================
// 合法着法生成
// 做法：
// 1. 先生成所有伪合法着法
// 2. 对每一步 do_move
// 3. 检查走完后自己王是否被将军
// 4. 再 undo_move
//
// 这样成本比较高，所以一般不适合深搜每层都大量调用
// 更适合根节点、合法性校验、UCI position 验证等场景
// =====================
void generate_legal(Position& pos, std::vector<Move>& legal) {
    static thread_local std::vector<Move> pseudo;

    generate_pseudo_legal(pos, pseudo);

    legal.clear();
    legal.reserve(pseudo.size());

    Color us = pos.side;

    for (Move m : pseudo) {
        // 王车易位需要额外检查路径攻击情况
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
// 先生成全部合法着法，再过滤出：
// 1. 普通吃子
// 2. 吃过路兵
// 3. 升变
//
// 注意：这里把“升变”也算进来了，哪怕它是非吃子升变
// 因为很多引擎在静态搜索里也会想特殊处理升变
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