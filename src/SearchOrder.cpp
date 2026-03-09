#include "SearchCore.h"

namespace search {

void Searcher::update_quiet_history(Color us, int prevFrom, int prevTo, int from, int to, int depth, bool good) {
    int ci = color_index(us);
    const int sign = good ? 1 : -1;
    const int hBonus = depth * depth * (good ? 16 : 2);
    const int cBonus = depth * depth * (good ? 12 : 1);

    update_stat(history[ci][from][to], sign * hBonus);
    if ((unsigned)prevFrom < 64u && (unsigned)prevTo < 64u)
        update_stat(contHist[ci][prevFrom][prevTo][from][to], sign * cBonus);
}

int Searcher::compute_lmr_reduction(int depth, int legalMovesSearched, bool inCheck, bool isQuiet, bool improving,
                                    bool isPvNode, Color us, int from, int to) {
    if (!g_params.enableLmr || depth < g_params.lmrMinDepth || inCheck || !isQuiet)
        return 0;

    int reduction = 1;
    if (legalMovesSearched > g_params.lmrMove1)
        reduction++;
    if (legalMovesSearched > g_params.lmrMove2)
        reduction++;
    if (depth >= g_params.lmrDepthForMove3 && legalMovesSearched > g_params.lmrMove3)
        reduction++;
    if (!improving)
        reduction++;
    if (isPvNode)
        reduction = std::max(0, reduction - 1);

    int ci = color_index(us);
    int h = history[ci][from][to] / 2;
    if (h < g_params.lmrHistoryLow)
        reduction++;
    if (h > g_params.lmrHistoryHigh)
        reduction = std::max(0, reduction - 1);

    if (!isPvNode) {
        if (EV_LMR_PROFILE == 0) {
            if (h < g_params.lmrHistoryLow) {
                if (depth >= 10 && legalMovesSearched > 10)
                    reduction++;
                if (depth >= 14 && legalMovesSearched > 16)
                    reduction++;
            }
        } else if (EV_LMR_PROFILE == 1) {
            if (h < g_params.lmrHistoryLow) {
                if (depth >= 8 && legalMovesSearched > 8)
                    reduction++;
                if (depth >= 12 && legalMovesSearched > 12)
                    reduction++;
                if (depth >= 16 && legalMovesSearched > 18)
                    reduction++;
            }
        } else {
            const int veryLow = std::max(1, g_params.lmrHistoryLow / 3);
            const bool veryLate = (legalMovesSearched > 20);
            if (veryLate && depth >= 12)
                reduction++;
            if (veryLate && depth >= 16 && h < g_params.lmrHistoryLow)
                reduction++;
            if (h < veryLow && depth >= 10 && legalMovesSearched > 10)
                reduction++;
        }
    }

    reduction = std::min(reduction, depth - 2);
    return std::max(0, reduction);
}

int Searcher::node_type_index(NodeContext::NodeType t) const {
    if (t == NodeContext::PV)
        return 0;
    if (t == NodeContext::CUT)
        return 1;
    return 2;
}

int Searcher::lmr_bucket(bool isKiller, bool isCounter, int quietScore) const {
    if (isKiller)
        return 0;
    if (isCounter)
        return 1;
    if (quietScore >= g_params.lmrHistoryLow)
        return 2;
    return 3;
}

int Searcher::quiet_bucket_score(Color us, int from, int to, int prevFrom, int prevTo) const {
    int ci = color_index(us);
    int sc = history[ci][from][to] / 2;
    if ((unsigned)prevFrom < 64u && (unsigned)prevTo < 64u)
        sc += contHist[ci][prevFrom][prevTo][from][to] / 2;
    return sc;
}

int Searcher::lmr_bucket_refined(bool isKiller, bool isCounter, bool recapture, bool givesCheck, int qScore) const {
    if (isKiller)
        return 0;
    if (isCounter)
        return 1;
    if (recapture || givesCheck)
        return 2;
    if (qScore >= g_params.lmrBucketHigh)
        return 2;
    return 3;
}

int Searcher::depth_bin64(int d) const {
    if (d < 0)
        return 0;
    if (d > 63)
        return 63;
    return d;
}

int Searcher::quiet_idx_bin(int idx1Based) const {
    if (idx1Based <= 1)
        return 0;
    if (idx1Based == 2)
        return 1;
    if (idx1Based <= 4)
        return 2;
    if (idx1Based <= 8)
        return 3;
    if (idx1Based <= 12)
        return 4;
    if (idx1Based <= 16)
        return 5;
    if (idx1Based <= 24)
        return 6;
    return 7;
}

int Searcher::classify_bucket(const Position& pos, Move m, Move ttMove, int ply, int prevFrom, int prevTo, int score,
                              bool scoreKnown) const {
    if (m == ttMove)
        return MB_TT;

    const bool capLike = is_capture(pos, m) || (flags_of(m) & MF_EP) || promo_of(m);
    if (capLike) {
        if (!scoreKnown)
            return MB_CAP_GOOD;
        return (score >= 50000000) ? MB_CAP_GOOD : MB_CAP_BAD;
    }

    ply = std::min(ply, 127);
    if (m == killer[0][ply] || m == killer[1][ply])
        return MB_QUIET_SPECIAL;
    if ((unsigned)prevFrom < 64u && (unsigned)prevTo < 64u && m == countermove[prevFrom][prevTo])
        return MB_QUIET_SPECIAL;
    return MB_QUIET;
}

Searcher::NodeContext Searcher::make_node_context(const Position& pos, int depth, int alpha, int beta, int ply,
                                                  bool inCheck, int staticEval, bool improving, bool ttHit,
                                                  const TTEntry& te) const {
    NodeContext ctx{};
    ctx.depth = depth;
    ctx.ply = ply;
    ctx.inCheck = inCheck;
    ctx.staticEval = staticEval;
    ctx.improving = improving;
    ctx.ttHit = ttHit;

    if (beta - alpha > 1)
        ctx.nodeType = NodeContext::PV;
    else
        ctx.nodeType = (staticEval >= beta ? NodeContext::CUT : NodeContext::ALL);

    if (ttHit) {
        ctx.ttDepth = te.depth;
        ctx.ttBound = te.flag;
        int conf = 0;
        if (te.depth >= depth)
            conf++;
        if (te.flag == TT_EXACT)
            conf += 2;
        else if (te.flag == TT_BETA || te.flag == TT_ALPHA)
            conf += 1;
        ctx.ttConfidence = (uint8_t)clampi(conf, 0, 3);
    }

    int nonPawn = 0;
    int major = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;
        PieceType pt = type_of(p);
        if (pt == KING || pt == PAWN)
            continue;
        nonPawn++;
        if (pt == ROOK || pt == QUEEN)
            major++;
    }

    if (major == 0 && nonPawn <= 4)
        ctx.endgameRisk = 2;
    else if (nonPawn <= 6)
        ctx.endgameRisk = 1;
    else
        ctx.endgameRisk = 0;

    return ctx;
}

int Searcher::move_score(const Position& pos, Move m, Move ttMove, int ply, int prevFrom, int prevTo) {
    ply = std::min(ply, 127);

    if (m == ttMove)
        return 1000000000;

    int from = from_sq(m), to = to_sq(m);
    Piece mover = pos.board[from];

    int sc = 0;

    if (promo_of(m))
        sc += 90000000;
    if (flags_of(m) & MF_CASTLE)
        sc += 30000000;

    if (is_capture(pos, m)) {
        sc += 50000000;

        Piece victim = pos.board[to];
        if (flags_of(m) & MF_EP)
            victim = make_piece(flip_color(pos.side), PAWN);

        int s = see_quick_main(pos, m);
        if (promo_of(m) || s < -250)
            s = see_full_main(pos, m);

        s = clampi(s, -500, 500);
        sc += s * 8000;
        sc += mvv_lva(victim, mover) * 200;
        return sc;
    }

    if (m == killer[0][ply])
        sc += 20000000;
    else if (m == killer[1][ply])
        sc += 15000000;

    const int ci = color_index(pos.side);
    sc += history[ci][from][to] / 2;

    if ((unsigned)prevFrom < 64u && (unsigned)prevTo < 64u) {
        if (m == countermove[prevFrom][prevTo])
            sc += 18000000;
        sc += contHist[ci][prevFrom][prevTo][from][to] / 4;
    }

    if (type_of(mover) == BISHOP)
        sc += 2000;
    if (type_of(mover) == KNIGHT)
        sc += 1000;

    if (type_of(mover) == KING && !(flags_of(m) & MF_CASTLE)) {
        if (ply < 12)
            sc -= 8000000;
        else
            sc -= 800000;
    }

    if (ply < 4 && type_of(mover) == PAWN && promo_of(m) == 0) {
        if (from == E2 && to == E4) sc += 12000;
        if (from == D2 && to == D4) sc += 12000;
        if (from == E7 && to == E5) sc += 12000;
        if (from == D7 && to == D5) sc += 12000;
        if (from == C2 && to == C4) sc += 7000;
        if (from == C7 && to == C5) sc += 7000;
    }

    return sc;
}

} // namespace search