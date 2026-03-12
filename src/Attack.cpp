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

// =====================
// 从 sq 出发，沿某个方向扫线，找第一个非空格
// 如果它是符合要求的滑子，则返回该格 bitboard，否则返回 0
// =====================
static inline Bitboard scan_slider_attacker(const Position& pos, int sq, int df, int dr, Color byColor, bool diag) {
    int f = file_of(sq) + df;
    int r = rank_of(sq) + dr;

    while ((unsigned)f < 8u && (unsigned)r < 8u) {
        int from = make_sq(f, r);
        Bitboard b = bb_sq(from);

        if (pos.occAll & b) {
            Piece p = pos.board[from];

            if (diag) {
                if (is_slider_bishop_queen(p, byColor))
                    return b;
            } else {
                if (is_slider_rook_queen(p, byColor))
                    return b;
            }
            return 0ULL;
        }

        f += df;
        r += dr;
    }

    return 0ULL;
}

// 返回一个 bitboard：
// 其中每个 1 位表示“该格上有一个 byColor 的棋子，正在攻击 sq”
Bitboard attackers_to_bb(const Position& pos, int sq, Color byColor) {
    if ((unsigned)sq >= 64u)
        return 0ULL;

    Bitboard atk = 0ULL;
    const Tables& tab = T();

    // =====================
    // 兵攻击
    // 反查法：
    // 若某个 byColor 的兵能攻击 sq，
    // 那么 sq 必须在它的 pawn attack 表里。
    //
    // 这里利用预处理表的逆向等价关系：
    // - 攻击 sq 的白兵，必定位于能从其位置打到 sq 的那些格
    // - 直接用 bitboard 交即可
    // =====================
    if (byColor == WHITE) {
        // 能攻击 sq 的白兵，来自 sq 的左下 / 右下
        int f = file_of(sq), r = rank_of(sq);
        if ((unsigned)(r - 1) < 8u) {
            if ((unsigned)(f - 1) < 8u) {
                int from = make_sq(f - 1, r - 1);
                atk |= bb_sq(from) & pos.pieces[WHITE][PAWN];
            }
            if ((unsigned)(f + 1) < 8u) {
                int from = make_sq(f + 1, r - 1);
                atk |= bb_sq(from) & pos.pieces[WHITE][PAWN];
            }
        }
    } else {
        // 能攻击 sq 的黑兵，来自 sq 的左上 / 右上
        int f = file_of(sq), r = rank_of(sq);
        if ((unsigned)(r + 1) < 8u) {
            if ((unsigned)(f - 1) < 8u) {
                int from = make_sq(f - 1, r + 1);
                atk |= bb_sq(from) & pos.pieces[BLACK][PAWN];
            }
            if ((unsigned)(f + 1) < 8u) {
                int from = make_sq(f + 1, r + 1);
                atk |= bb_sq(from) & pos.pieces[BLACK][PAWN];
            }
        }
    }

    // =====================
    // 马攻击
    // T().knight[sq] = “站在这些格上的马可以攻击 sq”
    // 直接与对方马 bitboard 取交
    // =====================
    atk |= tab.knight[sq] & pos.pieces[byColor][KNIGHT];

    // =====================
    // 王攻击
    // 同理直接取交
    // =====================
    atk |= tab.king[sq] & pos.pieces[byColor][KING];

    // =====================
    // 滑子攻击（象/车/后）
    // 从 sq 向外扫线，遇到第一堵子：
    // - 若是对应类型滑子，则它是攻击者
    // - 否则该线无攻击者
    // =====================
    atk |= scan_slider_attacker(pos, sq, +1, +1, byColor, true);
    atk |= scan_slider_attacker(pos, sq, -1, +1, byColor, true);
    atk |= scan_slider_attacker(pos, sq, +1, -1, byColor, true);
    atk |= scan_slider_attacker(pos, sq, -1, -1, byColor, true);

    atk |= scan_slider_attacker(pos, sq, +1, 0, byColor, false);
    atk |= scan_slider_attacker(pos, sq, -1, 0, byColor, false);
    atk |= scan_slider_attacker(pos, sq, 0, +1, byColor, false);
    atk |= scan_slider_attacker(pos, sq, 0, -1, byColor, false);

    return atk;
}

// 统计有多少个攻击者
int attackers_to_count(const Position& pos, int sq, Color byColor) {
    Bitboard b = attackers_to_bb(pos, sq, byColor);
    return bb_popcount(b);
}

// 判断某格是否被某方攻击
bool is_square_attacked(const Position& pos, int sq, Color byColor) {
    return attackers_to_bb(pos, sq, byColor) != 0ULL;
}

// 判断 sideToCheck 这一方是否在被将军
bool in_check(const Position& pos, Color sideToCheck) {
    int ksq = pos.kingSq[(int)sideToCheck];
    if (ksq < 0)
        ksq = pos.king_square(sideToCheck);

    if (ksq < 0)
        return false;

    return is_square_attacked(pos, ksq, flip(sideToCheck));
}

} // namespace attacks