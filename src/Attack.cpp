#include "Attack.h"

namespace attacks {

// =====================
// Tables 构造函数
// 负责一次性预计算：
// 1. 马攻击表
// 2. 王攻击表
// 3. 兵攻击表
// =====================
Tables::Tables() {
    for (int sq = 0; sq < 64; sq++) {
        const int f = file_of(sq);
        const int r = rank_of(sq);

        // =====================
        // 预处理马的攻击
        // =====================
        {
            static const int df[8] = {+1, +2, +2, +1, -1, -2, -2, -1};
            static const int dr[8] = {+2, +1, -1, -2, -2, -1, +1, +2};

            Bitboard b = 0;

            for (int i = 0; i < 8; i++) {
                int ff = f + df[i];
                int rr = r + dr[i];

                // 只有还在棋盘内才算合法目标格
                if ((unsigned)ff < 8u && (unsigned)rr < 8u) {
                    int to = make_sq(ff, rr);
                    b |= bb_sq(to);
                }
            }

            knight[sq] = b;
        }

        // =====================
        // 预处理王的攻击
        // =====================
        {
            static const int df[8] = {+1, +1, 0, -1, -1, -1, 0, +1};
            static const int dr[8] = {0, +1, +1, +1, 0, -1, -1, -1};

            Bitboard b = 0;

            for (int i = 0; i < 8; i++) {
                int ff = f + df[i];
                int rr = r + dr[i];

                if ((unsigned)ff < 8u && (unsigned)rr < 8u) {
                    int to = make_sq(ff, rr);
                    b |= bb_sq(to);
                }
            }

            king[sq] = b;
        }

        // =====================
        // 预处理兵的攻击
        // pawn[color][sq] 表示：
        // “一个 color 的兵站在 sq 时，它能攻击哪些格”
        // =====================
        {
            Bitboard w = 0, b = 0;

            // 白兵向上攻击，即 r+1
            if ((unsigned)(r + 1) < 8u) {
                if ((unsigned)(f - 1) < 8u)
                    w |= bb_sq(make_sq(f - 1, r + 1));
                if ((unsigned)(f + 1) < 8u)
                    w |= bb_sq(make_sq(f + 1, r + 1));
            }

            // 黑兵向下攻击，即 r-1
            if ((unsigned)(r - 1) < 8u) {
                if ((unsigned)(f - 1) < 8u)
                    b |= bb_sq(make_sq(f - 1, r - 1));
                if ((unsigned)(f + 1) < 8u)
                    b |= bb_sq(make_sq(f + 1, r - 1));
            }

            pawn[WHITE][sq] = w;
            pawn[BLACK][sq] = b;
        }
    }
}

// 返回全局唯一一份攻击表
// 函数内静态对象在 C++11 起是线程安全初始化的
const Tables& T() {
    static Tables t;
    return t;
}

// 返回一个 bitboard：
// 其中每个 1 位表示“该格上有一个 byColor 的棋子，正在攻击 sq”
Bitboard attackers_to_bb(const Position& pos, int sq, Color byColor) {
    if ((unsigned)sq >= 64u)
        return 0ULL;

    Bitboard atk = 0ULL;

    const int f = file_of(sq);
    const int r = rank_of(sq);

    // =====================
    // 兵攻击：这里做的是“反查”
    // 不是看 sq 上兵能打哪里，
    // 而是看“哪些兵能打到 sq”
    // =====================
    if (byColor == WHITE) {
        // 白兵从更低一排攻击上来
        // 所以攻击 sq 的白兵必须在：
        // (f-1, r-1) 或 (f+1, r-1)
        if ((unsigned)(r - 1) < 8u) {
            if ((unsigned)(f - 1) < 8u) {
                int from = make_sq(f - 1, r - 1);
                if (pos.board[from] == W_PAWN)
                    atk |= bb_sq(from);
            }
            if ((unsigned)(f + 1) < 8u) {
                int from = make_sq(f + 1, r - 1);
                if (pos.board[from] == W_PAWN)
                    atk |= bb_sq(from);
            }
        }
    } else {
        // 黑兵从更高一排攻击下来
        // 所以攻击 sq 的黑兵必须在：
        // (f-1, r+1) 或 (f+1, r+1)
        if ((unsigned)(r + 1) < 8u) {
            if ((unsigned)(f - 1) < 8u) {
                int from = make_sq(f - 1, r + 1);
                if (pos.board[from] == B_PAWN)
                    atk |= bb_sq(from);
            }
            if ((unsigned)(f + 1) < 8u) {
                int from = make_sq(f + 1, r + 1);
                if (pos.board[from] == B_PAWN)
                    atk |= bb_sq(from);
            }
        }
    }

    // =====================
    // 马攻击
    // 先用预处理表拿到“理论上能跳到 sq 的所有格”
    // 再检查这些格上是否真的站着 byColor 的马
    // =====================
    {
        Bitboard nmask = T().knight[sq];
        while (nmask) {
            int from = __builtin_ctzll(nmask);
            nmask &= nmask - 1;

            Piece p = pos.board[from];
            if (p != NO_PIECE && is_knight(p, byColor))
                atk |= bb_sq(from);
        }
    }

    // =====================
    // 王攻击
    // 同理，用预处理表反查
    // =====================
    {
        Bitboard kmask = T().king[sq];
        while (kmask) {
            int from = __builtin_ctzll(kmask);
            kmask &= kmask - 1;

            Piece p = pos.board[from];
            if (p != NO_PIECE && is_king(p, byColor))
                atk |= bb_sq(from);
        }
    }

    // =====================
    // 滑子攻击（象/车/后）
    // 思路：
    // 从目标格 sq 往某个方向一步一步扫，
    // 遇到第一个非空格后：
    //   - 如果它是对应类型的敌方滑子，则算攻击者
    //   - 不管是不是，扫线都到此停止
    // 因为“第一堵子”就决定了这条线是否通
    // =====================
    auto scan_dir = [&](int df, int dr, bool diag) {
        int ff = f + df;
        int rr = r + dr;

        while ((unsigned)ff < 8u && (unsigned)rr < 8u) {
            int from = make_sq(ff, rr);
            Piece p = pos.board[from];

            if (p != NO_PIECE) {
                if (diag) {
                    // 对角线方向：象 / 后
                    if (is_slider_bishop_queen(p, byColor))
                        atk |= bb_sq(from);
                } else {
                    // 直线方向：车 / 后
                    if (is_slider_rook_queen(p, byColor))
                        atk |= bb_sq(from);
                }
                break;
            }

            ff += df;
            rr += dr;
        }
    };

    // 四个对角方向
    scan_dir(+1, +1, true);
    scan_dir(-1, +1, true);
    scan_dir(+1, -1, true);
    scan_dir(-1, -1, true);

    // 四个正交方向
    scan_dir(+1, 0, false);
    scan_dir(-1, 0, false);
    scan_dir(0, +1, false);
    scan_dir(0, -1, false);

    return atk;
}

// 统计有多少个攻击者
int attackers_to_count(const Position& pos, int sq, Color byColor) {
    Bitboard b = attackers_to_bb(pos, sq, byColor);
    return (int)__builtin_popcountll(b);
}

// 判断某格是否被某方攻击
bool is_square_attacked(const Position& pos, int sq, Color byColor) {
    return attackers_to_bb(pos, sq, byColor) != 0ULL;
}

// 判断 sideToCheck 这一方是否在被将军
bool in_check(const Position& pos, Color sideToCheck) {
    int ksq = pos.king_square(sideToCheck);
    if (ksq < 0)
        return false;

    return is_square_attacked(pos, ksq, flip(sideToCheck));
}

} // namespace attacks