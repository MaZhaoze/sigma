#include "see_full.h"

#include <algorithm>
#include <cmath>

// =====================
// 升变编码转 PieceType
// 约定：1=N 2=B 3=R 4=Q
// 非法则返回 NONE
// =====================
PieceType promo_to_pt(int promoCode) {
    if (promoCode == 1) return KNIGHT;
    if (promoCode == 2) return BISHOP;
    if (promoCode == 3) return ROOK;
    if (promoCode == 4) return QUEEN;
    return NONE;
}

// =====================
// 从 64 格棋盘数组生成 occupancy bitboard
// 某格非空就置 1
// =====================
U64 occ_from_board(const Piece board[64]) {
    U64 occ = 0;
    for (int sq = 0; sq < 64; ++sq) {
        if (board[sq] != NO_PIECE)
            occ |= bb_sq(sq);
    }
    return occ;
}

// 判断格号是否在棋盘内
bool on_board(int sq) {
    return (unsigned)sq < 64u;
}

// =====================
// 收集所有“兵攻击 toSq”的攻击者
// 注意这里做的是反查：
// 不是问 toSq 上的兵打哪，
// 而是问“哪些兵能打到 toSq”
// =====================
void add_pawn_attackers_to(U64& attackers, const Piece b[64], int toSq) {
    int f = file_of(toSq);

    // 白兵：如果它能攻击到 toSq，那么它应在 toSq-9 或 toSq-7
    if (f > 0) {
        int s = toSq - 9;
        if (on_board(s) && b[s] == W_PAWN)
            attackers |= bb_sq(s);
    }
    if (f < 7) {
        int s = toSq - 7;
        if (on_board(s) && b[s] == W_PAWN)
            attackers |= bb_sq(s);
    }

    // 黑兵：如果它能攻击到 toSq，那么它应在 toSq+7 或 toSq+9
    if (f > 0) {
        int s = toSq + 7;
        if (on_board(s) && b[s] == B_PAWN)
            attackers |= bb_sq(s);
    }
    if (f < 7) {
        int s = toSq + 9;
        if (on_board(s) && b[s] == B_PAWN)
            attackers |= bb_sq(s);
    }
}

// =====================
// 收集所有“马攻击 toSq”的攻击者
// 先用偏移枚举潜在来源格，再用 df/dr 防止绕边
// =====================
void add_knight_attackers_to(U64& attackers, const Piece b[64], int toSq) {
    static const int offs[8] = {+17, +15, +10, +6, -6, -10, -15, -17};

    int tf = file_of(toSq), tr = rank_of(toSq);

    for (int k = 0; k < 8; ++k) {
        int s = toSq + offs[k];
        if (!on_board(s))
            continue;

        int sf = file_of(s), sr = rank_of(s);
        int df = std::abs(sf - tf), dr = std::abs(sr - tr);

        if (!((df == 1 && dr == 2) || (df == 2 && dr == 1)))
            continue;

        Piece p = b[s];
        if (p == W_KNIGHT || p == B_KNIGHT)
            attackers |= bb_sq(s);
    }
}

// =====================
// 收集所有“王攻击 toSq”的攻击者
// =====================
void add_king_attackers_to(U64& attackers, const Piece b[64], int toSq) {
    static const int offs[8] = {+1, -1, +8, -8, +9, +7, -7, -9};

    int tf = file_of(toSq), tr = rank_of(toSq);

    for (int k = 0; k < 8; ++k) {
        int s = toSq + offs[k];
        if (!on_board(s))
            continue;

        int sf = file_of(s), sr = rank_of(s);
        if (std::abs(sf - tf) > 1 || std::abs(sr - tr) > 1)
            continue;

        Piece p = b[s];
        if (p == W_KING || p == B_KING)
            attackers |= bb_sq(s);
    }
}

// =====================
// 沿一条射线找“第一个阻挡子”
// 如果它是对应类型的滑子（象/后 或 车/后），
// 就把它记为攻击者
//
// 注意：SEE 这里只关心“第一挡”
// 因为更深的滑子只有在前面的子被换掉后才可能显露，
// 后面循环重算 attackers_to_sq 时会自然体现出来
// =====================
void ray_first_attacker(U64& attackers, const Piece b[64], int toSq, int df, int dr, bool diag) {
    int f = file_of(toSq);
    int r = rank_of(toSq);

    for (;;) {
        f += df;
        r += dr;

        if ((unsigned)f >= 8u || (unsigned)r >= 8u)
            break;

        int s = make_sq(f, r);
        Piece p = b[s];

        if (p == NO_PIECE)
            continue;

        PieceType pt = type_of(p);
        if (diag) {
            if (pt == BISHOP || pt == QUEEN)
                attackers |= bb_sq(s);
        } else {
            if (pt == ROOK || pt == QUEEN)
                attackers |= bb_sq(s);
        }

        break; // 第一堵子就决定这条线
    }
}

// =====================
// 返回“所有攻击 toSq 的棋子”的 bitboard
// 不分颜色
// =====================
U64 attackers_to_sq(const Piece b[64], int toSq) {
    U64 att = 0;

    add_pawn_attackers_to(att, b, toSq);
    add_knight_attackers_to(att, b, toSq);
    add_king_attackers_to(att, b, toSq);

    // 四个对角方向
    ray_first_attacker(att, b, toSq, +1, +1, true);
    ray_first_attacker(att, b, toSq, -1, +1, true);
    ray_first_attacker(att, b, toSq, +1, -1, true);
    ray_first_attacker(att, b, toSq, -1, -1, true);

    // 四个正交方向
    ray_first_attacker(att, b, toSq, +1, 0, false);
    ray_first_attacker(att, b, toSq, -1, 0, false);
    ray_first_attacker(att, b, toSq, 0, +1, false);
    ray_first_attacker(att, b, toSq, 0, -1, false);

    return att;
}

// =====================
// 从攻击者集合中筛出指定颜色那一方的攻击者
// =====================
U64 color_attackers(U64 attackers, const Piece b[64], Color c) {
    U64 res = 0;
    U64 tmp = attackers;

    while (tmp) {
        int sq = pop_lsb(tmp);
        Piece p = b[sq];
        if (p != NO_PIECE && color_of(p) == c)
            res |= bb_sq(sq);
    }

    return res;
}

// =====================
// 找到某一方攻击者中“价值最低”的那个格子
// SEE 的标准做法就是轮流用最便宜的子去换
// =====================
int least_valuable_attacker_sq(U64 attackersSide, const Piece b[64]) {
    int bestSq = -1;
    int bestV = 1000000000;

    U64 tmp = attackersSide;
    while (tmp) {
        int sq = pop_lsb(tmp);
        Piece p = b[sq];
        if (p == NO_PIECE)
            continue;

        int v = piece_value(p);
        if (v < bestV) {
            bestV = v;
            bestSq = sq;
        }
    }

    return bestSq;
}

// =====================
// 判断一个兵从 fromSq 吃到 toSq 后是否会升变
// 注意这里只处理“吃子升变”这个场景
// =====================
bool pawn_promo_by_move(Color side, int fromSq, int toSq) {
    int tr = rank_of(toSq);

    if (side == WHITE) {
        return tr == 7 && rank_of(fromSq) == 6;
    } else {
        return tr == 0 && rank_of(fromSq) == 1;
    }
}

// =====================
// 完整 SEE（Static Exchange Evaluation）
//
// 作用：估计当前这一步 m 在交换序列中，
// 最终对先手一方的净得失是多少。
//
// 返回值 > 0：通常表示这步交换赚钱
// 返回值 < 0：通常表示亏 material
//
// 这里用的是经典 swap-list / backward induction 思路：
// 1. 先模拟第一步吃子
// 2. 双方轮流用“最便宜攻击者”继续吃回
// 3. 记录 gain 数组
// 4. 倒推得到最优交换结果
// =====================
int see_full(const Position& pos, Move m) {
    if (!m)
        return 0;

    // 易位不做 SEE
    if (flags_of(m) & MF_CASTLE)
        return 0;

    const int from = from_sq(m);
    const int to = to_sq(m);

    if ((unsigned)from >= 64u || (unsigned)to >= 64u)
        return 0;

    // 在局面副本上做交换模拟，避免改到真实局面
    Piece board[64];
    for (int i = 0; i < 64; ++i)
        board[i] = pos.board[i];

    Piece mover = board[from];
    if (mover == NO_PIECE)
        return 0;

    const Color us = pos.side;

    // --------
    // 第一步吃到的子值
    // 吃过路兵时目标格 to 本来是空的，所以特殊处理
    // --------
    int capturedV = 0;
    if (flags_of(m) & MF_EP) {
        capturedV = piece_value_pt(PAWN);
    } else {
        capturedV = piece_value(board[to]);
    }

    // --------
    // 先把第一步吃子真正应用到副本棋盘上
    // --------

    // 去掉被吃子
    if (flags_of(m) & MF_EP) {
        int capSq = (us == WHITE) ? (to - 8) : (to + 8);
        if (on_board(capSq))
            board[capSq] = NO_PIECE;
    } else {
        board[to] = NO_PIECE;
    }

    // 再把攻击方棋子从 from 移到 to
    int pr = promo_of(m);
    board[from] = NO_PIECE;

    Piece placed = mover;
    if (pr) {
        // 如果第一步本身是升变，则 to 上放升变后的子
        placed = make_piece(us, promo_to_pt(pr));
    }
    board[to] = placed;

    // --------
    // gain[d] 表示交换序列第 d 层的即时收益
    // gain[0] 就是第一步先手吃到的子值
    // --------
    int gain[32];
    int d = 0;
    gain[0] = capturedV;

    // 轮到对方来吃回
    Color side = (us == WHITE ? BLACK : WHITE);

    // 当前 to 格上站着的“下一步要被吃的子”
    Piece onTo = board[to];

    while (true) {
        // 重新计算所有能攻击 to 的棋子
        // 注意这里每轮都重算，因为前面的交换可能打开新射线
        U64 allAtt = attackers_to_sq(board, to);
        U64 attSide = color_attackers(allAtt, board, side);

        if (!attSide)
            break;

        // 当前这一方用最便宜的子来吃
        int aSq = least_valuable_attacker_sq(attSide, board);
        if (aSq < 0)
            break;

        Piece aPiece = board[aSq];

        // 这一层吃到的，是当前站在 to 上那枚子的价值
        int capVal = piece_value(onTo);

        // 如果当前吃回者是兵，并且吃完会升变，
        // 那么“下一层”对方面对的就是一只后（这里默认升后）
        bool promoCap = (type_of(aPiece) == PAWN && pawn_promo_by_move(side, aSq, to));

        d++;
        if (d >= 31)
            break;

        // 标准 SEE 递推：
        // gain[d] = 这一步吃到的东西 - 上一步之后对方能拿回去的收益
        gain[d] = capVal - gain[d - 1];

        // 在副本棋盘上执行这一步吃回：aSq -> to
        board[aSq] = NO_PIECE;
        board[to] = aPiece;

        // 如果这是兵吃到终排，则默认升后
        if (promoCap) {
            board[to] = make_piece(side, QUEEN);
        }

        // 更新“下一层 to 上的子是谁”
        onTo = board[to];

        // 换边继续
        side = (side == WHITE ? BLACK : WHITE);
    }

    // --------
    // 倒推最优结果
    //
    // 经典 backward induction：
    // 当前层可以选择“继续交换”或“停止交换”，
    // 因此要做 max(gain[i], -gain[i+1])
    // --------
    for (int i = d - 1; i >= 0; --i) {
        gain[i] = std::max(gain[i], -gain[i + 1]);
    }

    return gain[0];
}

// 判断 SEE 是否至少达到某个阈值
bool see_ge(const Position& pos, Move m, int threshold) {
    return see_full(pos, m) >= threshold;
}