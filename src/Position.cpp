#include "Position.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

#include "ZobristTables.h"

namespace {

// =====================
// bitboard 增量维护小工具
// 说明：
// 1. 这些函数只负责维护 bitboard，不碰 board[]
// 2. 调用者负责保证参数合法
// =====================
inline void bb_add_piece(Position& pos, int sq, Piece p) {
    if (p == NO_PIECE)
        return;

    const Color c = color_of(p);
    const PieceType pt = type_of(p);
    const Bitboard b = sq_bb(sq);

    pos.pieces[(int)c][(int)pt] |= b;
    pos.occ[(int)c] |= b;
    pos.occAll |= b;

    if (pt == KING)
        pos.kingSq[(int)c] = sq;
}

inline void bb_remove_piece(Position& pos, int sq, Piece p) {
    if (p == NO_PIECE)
        return;

    const Color c = color_of(p);
    const PieceType pt = type_of(p);
    const Bitboard b = sq_bb(sq);

    pos.pieces[(int)c][(int)pt] &= ~b;
    pos.occ[(int)c] &= ~b;
    pos.occAll &= ~b;

    if (pt == KING)
        pos.kingSq[(int)c] = -1;
}

inline void bb_move_piece(Position& pos, int from, int to, Piece p) {
    bb_remove_piece(pos, from, p);
    bb_add_piece(pos, to, p);
}

} // namespace

// =====================
// 构造函数
// 默认清空后设成初始局面
// =====================
Position::Position() {
    clear();
    set_startpos();
}

// =====================
// 清空局面
// 注意这是“空棋盘”而不是初始局面
// =====================
void Position::clear() {
    for (int i = 0; i < 64; i++)
        board[i] = NO_PIECE;

    side = WHITE;
    castlingRights = CR_NONE;
    epSquare = -1;
    halfmoveClock = 0;
    fullmoveNumber = 1;
    zobKey = 0;

    // bitboard 同步清空
    for (int c = 0; c < BB_COLOR_NB; ++c) {
        occ[c] = 0ULL;
        kingSq[c] = -1;
        for (int pt = 0; pt < BB_PTYPE_NB; ++pt)
            pieces[c][pt] = 0ULL;
    }
    occAll = 0ULL;
}

// =====================
// 棋子字符 -> 内部 Piece 枚举
// 主要供 FEN 解析使用
// =====================
Piece Position::char_to_piece(char c) {
    switch (c) {
    case 'P': return W_PAWN;
    case 'N': return W_KNIGHT;
    case 'B': return W_BISHOP;
    case 'R': return W_ROOK;
    case 'Q': return W_QUEEN;
    case 'K': return W_KING;
    case 'p': return B_PAWN;
    case 'n': return B_KNIGHT;
    case 'b': return B_BISHOP;
    case 'r': return B_ROOK;
    case 'q': return B_QUEEN;
    case 'k': return B_KING;
    default:  return NO_PIECE;
    }
}

// =====================
// 代数坐标转格号
// 例如 "e4" -> 某个 0..63 编号
// 非法则返回 -1
// =====================
int Position::algebraic_to_sq(const std::string& s) {
    if (s.size() != 2)
        return -1;

    char f = s[0], r = s[1];
    if (f < 'a' || f > 'h')
        return -1;
    if (r < '1' || r > '8')
        return -1;

    return make_sq(f - 'a', r - '1');
}

// =====================
// 格号转代数坐标
// 例如 0 -> "a1"
// 非法格返回 "-"
// =====================
std::string Position::sq_to_algebraic(int sq) {
    if (sq < 0 || sq >= 64)
        return "-";

    std::string s;
    s += char('a' + file_of(sq));
    s += char('1' + rank_of(sq));
    return s;
}

// =====================
// 全量重算 zobrist
// 一般在：
// 1. 初始化局面后
// 2. FEN 设置后
// 3. 调试校验时
// 使用
//
// 真正搜索时更常用增量更新
// =====================
void Position::recompute_zobrist() {
    uint64_t k = 0;

    for (int sq = 0; sq < 64; sq++) {
        Piece p = board[sq];
        if (p == NO_PIECE)
            continue;

        int pi = (int)p;
        if ((unsigned)pi >= 16u)
            continue;

        k ^= g_zob.psq[pi][sq];
    }

    // 侧面方
    if (side == BLACK)
        k ^= g_zob.sideKey;

    // 易位权
    k ^= g_zob.castleKey[castlingRights & 15];

    // EP 文件
    if (epSquare != -1)
        k ^= g_zob.epKey[file_of(epSquare) & 7];

    zobKey = k;
}

// =====================
// 增量更新 zobrist
// 注意：
// 这个函数假设 board / side / castlingRights / epSquare
// 都已经被 do_move 改成“走完之后”的状态了
// =====================
void Position::apply_zobrist_delta_after_move(const Undo& u, Move m) {
    uint64_t k = u.prevKey;

    // 先处理 EP key：去掉旧的，加上新的
    if (u.prevEpSquare != -1)
        k ^= g_zob.epKey[file_of(u.prevEpSquare) & 7];
    if (epSquare != -1)
        k ^= g_zob.epKey[file_of(epSquare) & 7];

    // 处理易位权 key：去掉旧的，加上新的
    k ^= g_zob.castleKey[u.prevCastling & 15];
    k ^= g_zob.castleKey[castlingRights & 15];

    // side 翻转
    k ^= g_zob.sideKey;

    const int from = from_sq(m);
    const int to = to_sq(m);

    // 移动棋子从 from 消失
    {
        int pi = (int)u.moved;
        if ((unsigned)pi < 16u)
            k ^= g_zob.psq[pi][from];
    }

    // 被吃子消失
    // 吃过路兵和普通吃子的位置不同
    if (flags_of(m) & MF_EP) {
        if (u.epCapturedSq != -1 && u.captured != NO_PIECE) {
            int pi = (int)u.captured;
            if ((unsigned)pi < 16u)
                k ^= g_zob.psq[pi][u.epCapturedSq];
        }
    } else if (u.captured != NO_PIECE) {
        int pi = (int)u.captured;
        if ((unsigned)pi < 16u)
            k ^= g_zob.psq[pi][to];
    }

    // 最终落在 to 的棋子
    // 这里很关键：如果是升变，to 上已经不是原来的兵了
    {
        Piece finalP = board[to];
        int pi = (int)finalP;
        if ((unsigned)pi < 16u)
            k ^= g_zob.psq[pi][to];
    }

    // 王车易位时，车也要在 zobrist 里移动
    if (flags_of(m) & MF_CASTLE) {
        if (u.rookFrom != -1 && u.rookTo != -1) {
            Piece rook = make_piece(u.prevSide, ROOK);
            int pi = (int)rook;
            if ((unsigned)pi < 16u) {
                k ^= g_zob.psq[pi][u.rookFrom];
                k ^= g_zob.psq[pi][u.rookTo];
            }
        }
    }

    zobKey = k;
}

// =====================
// 从当前 board[64] 全量重建 bitboard
// 第一阶段优先保证正确性，
// 后续再考虑 do_move/undo_move 的增量维护
// =====================
void Position::rebuild_bitboards() {
    for (int c = 0; c < BB_COLOR_NB; ++c) {
        occ[c] = 0ULL;
        kingSq[c] = -1;
        for (int pt = 0; pt < BB_PTYPE_NB; ++pt)
            pieces[c][pt] = 0ULL;
    }
    occAll = 0ULL;

    for (int sq = 0; sq < 64; ++sq) {
        Piece p = board[sq];
        if (p == NO_PIECE)
            continue;

        Color c = color_of(p);
        PieceType pt = type_of(p);

        Bitboard b = sq_bb(sq);
        pieces[(int)c][(int)pt] |= b;
        occ[(int)c] |= b;
        occAll |= b;

        if (pt == KING)
            kingSq[(int)c] = sq;
    }
}

// =====================
// 验证 board[64] 与 bitboard 是否一致
// 调试阶段非常有用
// =====================
bool Position::verify_bitboards() const {
    Bitboard p[BB_COLOR_NB][BB_PTYPE_NB]{};
    Bitboard o[BB_COLOR_NB]{};
    Bitboard all = 0ULL;
    int ks[BB_COLOR_NB] = {-1, -1};

    for (int sq = 0; sq < 64; ++sq) {
        Piece piece = board[sq];
        if (piece == NO_PIECE)
            continue;

        Color c = color_of(piece);
        PieceType pt = type_of(piece);
        Bitboard b = sq_bb(sq);

        p[(int)c][(int)pt] |= b;
        o[(int)c] |= b;
        all |= b;

        if (pt == KING)
            ks[(int)c] = sq;
    }

    for (int c = 0; c < BB_COLOR_NB; ++c) {
        if (occ[c] != o[c])
            return false;
        if (kingSq[c] != ks[c])
            return false;

        for (int pt = 0; pt < BB_PTYPE_NB; ++pt) {
            if (pieces[c][pt] != p[c][pt])
                return false;
        }
    }

    return occAll == all;
}

// =====================
// 设为标准初始局面
// =====================
void Position::set_startpos() {
    clear();

    // 白方后排
    board[A1] = W_ROOK;
    board[B1] = W_KNIGHT;
    board[C1] = W_BISHOP;
    board[D1] = W_QUEEN;
    board[E1] = W_KING;
    board[F1] = W_BISHOP;
    board[G1] = W_KNIGHT;
    board[H1] = W_ROOK;

    // 白兵
    for (int f = 0; f < 8; f++)
        board[make_sq(f, 1)] = W_PAWN;

    // 黑方后排
    board[A8] = B_ROOK;
    board[B8] = B_KNIGHT;
    board[C8] = B_BISHOP;
    board[D8] = B_QUEEN;
    board[E8] = B_KING;
    board[F8] = B_BISHOP;
    board[G8] = B_KNIGHT;
    board[H8] = B_ROOK;

    // 黑兵
    for (int f = 0; f < 8; f++)
        board[make_sq(f, 6)] = B_PAWN;

    side = WHITE;
    castlingRights = CR_WK | CR_WQ | CR_BK | CR_BQ;
    epSquare = -1;
    halfmoveClock = 0;
    fullmoveNumber = 1;

    rebuild_bitboards();
    recompute_zobrist();
}

// =====================
// FEN 解析
// 支持完整 6 项：
// 1. 棋盘
// 2. 行棋方
// 3. 易位权
// 4. EP
// 5. halfmove
// 6. fullmove
// =====================
void Position::set_fen(const std::string& fen) {
    clear();

    std::istringstream iss(fen);
    std::string boardPart, sidePart, castlingPart, epPart;

    iss >> boardPart >> sidePart >> castlingPart >> epPart >> halfmoveClock >> fullmoveNumber;

    // 如果 FEN 空掉了，直接回到初始局面
    if (boardPart.empty()) {
        set_startpos();
        return;
    }

    // --------
    // 1. 棋盘部分
    // FEN 从 a8 开始往后写
    // 我们这里 sq=56 对应 a8
    // --------
    int sq = 56;
    for (char c : boardPart) {
        if (c == '/') {
            sq -= 16;
            continue;
        }

        if (std::isdigit((unsigned char)c)) {
            sq += (c - '0');
            continue;
        }

        Piece p = char_to_piece(c);
        if (sq >= 0 && sq < 64)
            board[sq] = p;
        sq++;
    }

    // --------
    // 2. 行棋方
    // --------
    side = (sidePart == "b") ? BLACK : WHITE;

    // --------
    // 3. 易位权
    // --------
    castlingRights = CR_NONE;
    if (!castlingPart.empty() && castlingPart != "-") {
        for (char c : castlingPart) {
            if (c == 'K') castlingRights |= CR_WK;
            else if (c == 'Q') castlingRights |= CR_WQ;
            else if (c == 'k') castlingRights |= CR_BK;
            else if (c == 'q') castlingRights |= CR_BQ;
        }
    }

    // --------
    // 4. EP
    // --------
    if (epPart == "-" || epPart.empty())
        epSquare = -1;
    else
        epSquare = algebraic_to_sq(epPart);

    // --------
    // 5/6. 半回合时钟 & 全回合数
    // 缺失/非法时做兜底
    // --------
    if (halfmoveClock < 0)
        halfmoveClock = 0;
    if (fullmoveNumber <= 0)
        fullmoveNumber = 1;

    rebuild_bitboards();
    recompute_zobrist();
}

// =====================
// 王动了 -> 该方易位权全没
// =====================
void Position::remove_castling_for_king(Color c) {
    if (c == WHITE)
        castlingRights &= ~(CR_WK | CR_WQ);
    else
        castlingRights &= ~(CR_BK | CR_BQ);
}

// =====================
// 如果某个原始车格上的车移动/被吃，
// 对应方向的易位权就没了
// =====================
void Position::remove_castling_for_rook_square(int sq) {
    if (sq == H1)
        castlingRights &= ~CR_WK;
    else if (sq == A1)
        castlingRights &= ~CR_WQ;
    else if (sq == H8)
        castlingRights &= ~CR_BK;
    else if (sq == A8)
        castlingRights &= ~CR_BQ;
}

// =====================
// 执行一步棋
// 支持：
// - 普通走子
// - 吃子
// - 升变
// - 吃过路兵
// - 王车易位
//
// 返回 Undo，用于之后 undo_move
// =====================
Undo Position::do_move(Move m) {
    Undo u;

    // 先保存旧状态
    u.prevSide = side;
    u.prevCastling = castlingRights;
    u.prevEpSquare = epSquare;
    u.prevHalfmove = halfmoveClock;
    u.prevFullmove = fullmoveNumber;
    u.prevKey = zobKey;

    const int from = from_sq(m);
    const int to = to_sq(m);

    u.moved = board[from];
    u.captured = board[to];

    Piece movedPiece = board[from];
    PieceType movedType = type_of(movedPiece);
    Color us = side;

    // 默认先清 EP
    epSquare = -1;

    // halfmove：兵走或发生吃子则清零，否则加一
    bool isCapture = (u.captured != NO_PIECE) || (flags_of(m) & MF_EP);
    if (movedType == PAWN || isCapture)
        halfmoveClock = 0;
    else
        halfmoveClock++;

    // --------
    // 更新易位权：动王、动车
    // --------
    if (movedType == KING) {
        remove_castling_for_king(us);
    } else if (movedType == ROOK) {
        remove_castling_for_rook_square(from);
    }

    // 如果吃掉了对方初始车格上的车，也要取消对方对应易位权
    if (u.captured != NO_PIECE && type_of(u.captured) == ROOK) {
        remove_castling_for_rook_square(to);
    }

    // =====================
    // 吃过路兵
    // =====================
    if (flags_of(m) & MF_EP) {
        // 被吃兵不在 to，而是在 to 后面一格
        int capSq = (us == WHITE) ? (to - 8) : (to + 8);
        u.epCapturedSq = capSq;
        u.captured = board[capSq];

        // bitboard / board：移除被吃兵
        bb_remove_piece(*this, capSq, u.captured);
        board[capSq] = NO_PIECE;

        // bitboard / board：移动兵
        bb_move_piece(*this, from, to, movedPiece);
        board[to] = movedPiece;
        board[from] = NO_PIECE;
    }

    // =====================
    // 王车易位
    // =====================
    else if (flags_of(m) & MF_CASTLE) {
        // 先动王
        bb_move_piece(*this, from, to, movedPiece);
        board[to] = movedPiece;
        board[from] = NO_PIECE;

        // 再确定车怎么动
        if (us == WHITE) {
            if (from == E1 && to == G1) {
                u.rookFrom = H1;
                u.rookTo = F1;
            } else if (from == E1 && to == C1) {
                u.rookFrom = A1;
                u.rookTo = D1;
            }
        } else {
            if (from == E8 && to == G8) {
                u.rookFrom = H8;
                u.rookTo = F8;
            } else if (from == E8 && to == C8) {
                u.rookFrom = A8;
                u.rookTo = D8;
            }
        }

        // 真正动车
        if (u.rookFrom != -1 && u.rookTo != -1) {
            Piece rook = board[u.rookFrom];
            bb_move_piece(*this, u.rookFrom, u.rookTo, rook);
            board[u.rookTo] = rook;
            board[u.rookFrom] = NO_PIECE;
        }
    }

    // =====================
    // 普通走子 / 普通吃子 / 升变
    // =====================
    else {
        // 普通吃子：先移除目标格的被吃子
        if (u.captured != NO_PIECE)
            bb_remove_piece(*this, to, u.captured);

        board[to] = movedPiece;
        board[from] = NO_PIECE;

        // 升变：兵到达底线后改成新子
        int promo = promo_of(m);
        if (promo && movedType == PAWN) {
            PieceType pt = QUEEN;
            if (promo == 1) pt = KNIGHT;
            else if (promo == 2) pt = BISHOP;
            else if (promo == 3) pt = ROOK;
            else if (promo == 4) pt = QUEEN;

            // bitboard：兵从 from 消失，升变子出现在 to
            bb_remove_piece(*this, from, movedPiece);
            bb_add_piece(*this, to, make_piece(us, pt));

            board[to] = make_piece(us, pt);
        } else {
            // bitboard：普通移动
            bb_move_piece(*this, from, to, movedPiece);
        }

        // 兵双步时设置 EP 格
        if (movedType == PAWN) {
            int dr = rank_of(to) - rank_of(from);
            if (us == WHITE && dr == 2)
                epSquare = from + 8;
            else if (us == BLACK && dr == -2)
                epSquare = from - 8;
        }
    }

    // 切换行棋方
    side = ~side;

    // 黑方走完一步后，全回合数 +1
    if (us == BLACK)
        fullmoveNumber++;

    // 增量更新 zobrist
    apply_zobrist_delta_after_move(u, m);

    return u;
}

// =====================
// 悔棋
// 核心原则：
// 1. 状态全部恢复成 Undo 里记录的旧值
// 2. 棋盘按不同特殊情况分别恢复
// 3. zobrist 直接恢复 prevKey，最稳
// =====================
void Position::undo_move(Move m, const Undo& u) {
    const int from = from_sq(m);
    const int to = to_sq(m);

    // 先恢复全局状态
    castlingRights = u.prevCastling;
    epSquare = u.prevEpSquare;
    halfmoveClock = u.prevHalfmove;
    fullmoveNumber = u.prevFullmove;
    side = u.prevSide;

    // 如果是易位，先把车移回去
    if (u.rookFrom != -1 && u.rookTo != -1) {
        Piece rook = board[u.rookTo];
        bb_move_piece(*this, u.rookTo, u.rookFrom, rook);
        board[u.rookFrom] = rook;
        board[u.rookTo] = NO_PIECE;
    }

    // 如果是吃过路兵，恢复方式和普通吃子不同
    if (u.epCapturedSq != -1) {
        // 移回走子的兵
        bb_move_piece(*this, to, from, u.moved);
        board[from] = u.moved;
        board[to] = NO_PIECE; // EP 落点本来是空的

        // 恢复被吃兵
        bb_add_piece(*this, u.epCapturedSq, u.captured);
        board[u.epCapturedSq] = u.captured;

        zobKey = u.prevKey;
        return;
    }

    // 普通恢复（包括升变）
    // 如果刚才是升变，那么 to 上不是 u.moved，而是升变后的子
    if (promo_of(m) && type_of(u.moved) == PAWN) {
        Piece promoted = board[to];
        bb_remove_piece(*this, to, promoted);
        bb_add_piece(*this, from, u.moved);

        board[from] = u.moved;
        board[to] = NO_PIECE;
    } else {
        bb_move_piece(*this, to, from, u.moved);
        board[from] = u.moved;
        board[to] = NO_PIECE;
    }

    // 恢复普通吃子
    if (u.captured != NO_PIECE) {
        bb_add_piece(*this, to, u.captured);
        board[to] = u.captured;
    }

    zobKey = u.prevKey;
}

// =====================
// 找某方王的位置
// 主要给 in_check / 调试用
// =====================
int Position::king_square(Color c) const {
    // 优先返回 bitboard 维护的王格
    int ks = kingSq[(int)c];
    if (ks != -1)
        return ks;

    // 兜底：如果 bitboard 还没接上某些路径，就退回扫描
    Piece king = (c == WHITE) ? W_KING : B_KING;

    for (int i = 0; i < 64; i++) {
        if (board[i] == king)
            return i;
    }

    return -1;
}