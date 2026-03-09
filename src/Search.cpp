#include "SearchCore.h"

namespace search {

Undo Searcher::do_move_counted(Position& pos, Move m, bool inQ) {
    if (collect_stats()) {
        ss.makeCalls++;
        if (inQ)
            ss.makeQ++;
        else
            ss.makeMain++;
    }
    return pos.do_move(m);
}

int Searcher::see_quick_main(const Position& pos, Move m) {
    if (collect_stats())
        ss.seeCallsMain++;
    return see_quick(pos, m);
}

int Searcher::see_full_main(const Position& pos, Move m) {
    if (collect_stats())
        ss.seeCallsMain++;
    return see_full(pos, m);
}

int Searcher::see_quick_q(const Position& pos, Move m) {
    if (collect_stats())
        ss.seeCallsQ++;
    return see_quick(pos, m);
}

int Searcher::see_full_q(const Position& pos, Move m) {
    if (collect_stats())
        ss.seeCallsQ++;
    return see_full(pos, m);
}

uint64_t Searcher::compute_pinned_mask_for_side(const Position& pos, Color us) {
    const int ksq = pos.king_square(us);
    if ((unsigned)ksq >= 64u)
        return 0ULL;

    uint64_t pinned = 0ULL;
    const int kf = file_of(ksq);
    const int kr = rank_of(ksq);
    const Color them = flip_color(us);

    auto scan = [&](int df, int dr, bool diag) {
        int ff = kf + df;
        int rr = kr + dr;
        int ownSq = -1;

        while ((unsigned)ff < 8u && (unsigned)rr < 8u) {
            int sq = make_sq(ff, rr);
            Piece p = pos.board[sq];

            if (p != NO_PIECE) {
                if (color_of(p) == us) {
                    if (ownSq == -1)
                        ownSq = sq;
                    else
                        break;
                } else {
                    PieceType pt = type_of(p);
                    bool slider = diag ? (pt == BISHOP || pt == QUEEN)
                                       : (pt == ROOK || pt == QUEEN);
                    if (slider && ownSq != -1 && color_of(p) == them)
                        pinned |= (1ULL << ownSq);
                    break;
                }
            }

            ff += df;
            rr += dr;
        }
    };

    scan(+1, 0, false);
    scan(-1, 0, false);
    scan(0, +1, false);
    scan(0, -1, false);
    scan(+1, +1, true);
    scan(+1, -1, true);
    scan(-1, +1, true);
    scan(-1, -1, true);

    return pinned;
}

uint64_t Searcher::pinned_mask_for_ply(const Position& pos, Color us, int plyCtx) {
    if ((unsigned)plyCtx >= 256u)
        return compute_pinned_mask_for_side(pos, us);

    if (!pinnedMaskValid[plyCtx]) {
        pinnedMaskCache[plyCtx] = compute_pinned_mask_for_side(pos, us);
        pinnedMaskValid[plyCtx] = true;
        if (collect_stats())
            ss.pinCalc++;
    }
    return pinnedMaskCache[plyCtx];
}

bool Searcher::see_fast_non_negative(Position& pos, Move m, bool inQ) {
    const int to = to_sq(m);
    Undo u = do_move_counted(pos, m, inQ);
    const bool safe = !attacks::is_square_attacked(pos, to, pos.side);
    pos.undo_move(m, u);

    if (safe && collect_stats())
        ss.seeFastSafe++;
    return safe;
}

int Searcher::qsearch(Position& pos, int alpha, int beta, int ply, int lastTo, bool lastWasCap) {
    add_node();
    selDepthLocal = std::max(selDepthLocal, ply);

    if (collect_stats()) {
        ss.qNodes++;
        ss.qNodePly[depth_bin64(ply)]++;
    }

    const Color us = pos.side;
    const bool inCheck = attacks::in_check(pos, us);

    if ((unsigned)ply < 256u) {
        pinnedMaskValid[ply] = false;
        inCheckCache[ply] = inCheck;
        inCheckCacheValid[ply] = true;
    }

    if (ply >= 64)
        return eval::evaluate(pos);

    int stand = -INF;
    if (!inCheck) {
        stand = eval::evaluate(pos);
        if (stand >= beta)
            return beta;
        if (stand > alpha)
            alpha = stand;
    }

    auto& moves = plyMoves[ply];
    movegen::generate_pseudo_legal(pos, moves);

    auto& list = plyQList[ply];
    list.clear();
    list.reserve(moves.size());

    static const int PVV[7] = {0, 100, 320, 330, 500, 900, 0};

    constexpr int QUIET_CHECK_MAX_PLY = 1;
    constexpr int DELTA_MARGIN = 140;
    constexpr int SEE_CUT = -120;
    constexpr int SEE_FULL_TRIGGER = -240;

    const bool shallow = (ply <= 1);

    for (Move m : moves) {
        if (flags_of(m) & MF_CASTLE)
            continue;

        const bool promo = (promo_of(m) != 0);
        const bool cap = is_capture(pos, m) || (flags_of(m) & MF_EP);

        const bool quietCandidate = (!inCheck && !cap && !promo && ply < QUIET_CHECK_MAX_PLY);
        if (!inCheck && !(cap || promo || quietCandidate))
            continue;

        int gain = 0;
        Piece victim = NO_PIECE;

        if (cap) {
            int to = to_sq(m);
            if (flags_of(m) & MF_EP)
                gain += 100;
            else {
                victim = pos.board[to];
                gain += (victim == NO_PIECE ? 0 : PVV[type_of(victim)]);
            }
        }

        if (promo) {
            int pr = promo_of(m);
            int newv = (pr == 1 ? 320 : pr == 2 ? 330 : pr == 3 ? 500 : 900);
            gain += (newv - 100);
        }

        if (!inCheck && (cap || promo) && !shallow) {
            if (!promo && stand + gain + DELTA_MARGIN <= alpha)
                continue;

            if (!promo) {
                int sQ = see_quick_q(pos, m);

                if (sQ <= SEE_FULL_TRIGGER) {
                    bool bigVictim = false;
                    if (flags_of(m) & MF_EP)
                        bigVictim = false;
                    else if (victim != NO_PIECE && type_of(victim) >= ROOK)
                        bigVictim = true;

                    if (bigVictim) {
                        if (!see_fast_non_negative(pos, m, true)) {
                            int sF = see_full_q(pos, m);
                            if (sF < SEE_CUT)
                                continue;
                        }
                    } else {
                        if (sQ < SEE_CUT)
                            continue;
                    }
                } else if (sQ < SEE_CUT) {
                    continue;
                }
            }
        }

        if (quietCandidate && !shallow) {
            if (stand + 40 < alpha)
                continue;
        }

        int key = 0;
        if (promo)
            key += 400000;
        if (cap)
            key += 80000;
        key += gain * 300;

        if (lastWasCap && cap && lastTo >= 0 && to_sq(m) == lastTo)
            key += 220000;

        list.push_back(QNode{m, key, cap, promo, false});
    }

    if (list.empty()) {
        if (inCheck)
            return -MATE + ply;
        return alpha;
    }

    if (list.size() > 1) {
        std::sort(list.begin(), list.end(),
                  [](const QNode& a, const QNode& b) { return a.key > b.key; });
    }

    for (QNode& qn : list) {
        Move m = qn.m;

        Undo u = do_move_counted(pos, m, true);

        if (attacks::in_check(pos, us)) {
            pos.undo_move(m, u);
            continue;
        }

        if (!inCheck && !(qn.cap || qn.promo)) {
            if (!attacks::in_check(pos, pos.side)) {
                pos.undo_move(m, u);
                continue;
            }
        }

        const int nextLastTo = to_sq(m);
        const bool nextLastWasCap = qn.cap;

        int score = -qsearch(pos, -beta, -alpha, ply + 1, nextLastTo, nextLastWasCap);

        pos.undo_move(m, u);

        if (score >= beta)
            return beta;
        if (score > alpha)
            alpha = score;
    }

    return alpha;
}

void Searcher::do_null_move(Position& pos, NullMoveUndo& u) {
    u.ep = pos.epSquare;
    u.side = pos.side;
    u.key = pos.zobKey;

    uint64_t k = pos.zobKey;

    if (pos.epSquare != -1)
        k ^= g_zob.epKey[file_of(pos.epSquare) & 7];

    k ^= g_zob.sideKey;

    pos.epSquare = -1;
    pos.side = (pos.side == WHITE) ? BLACK : WHITE;
    pos.zobKey = k;
}

void Searcher::undo_null_move(Position& pos, const NullMoveUndo& u) {
    pos.epSquare = u.ep;
    pos.side = u.side;
    pos.zobKey = u.key;
}

bool Searcher::has_non_pawn_material(const Position& pos, Color c) {
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;

        if (c == WHITE) {
            int v = (int)p;
            if (v >= 1 && v <= 6) {
                PieceType pt = type_of(p);
                if (pt != PAWN && pt != KING)
                    return true;
            }
        } else {
            int v = (int)p;
            if (v >= 9 && v <= 14) {
                PieceType pt = type_of(p);
                if (pt != PAWN && pt != KING)
                    return true;
            }
        }
    }
    return false;
}

bool Searcher::is_legal_move_here(Position& pos, Move m, int plyCtx) {
    const bool cs = collect_stats();
    if (cs)
        ss.legCalls++;

    if (!move_sane_basic(pos, m)) {
        if (cs) {
            ss.legFail++;
            ss.legSlow++;
        }
        return false;
    }

    const Color us = pos.side;
    const int from = from_sq(m);
    const int to = to_sq(m);
    const Piece mover = pos.board[from];
    const bool isKing = (type_of(mover) == KING);
    const bool isEp = ((flags_of(m) & MF_EP) != 0);
    const bool isCastle = ((flags_of(m) & MF_CASTLE) != 0);
    const bool isCap = isEp || (pos.board[to] != NO_PIECE);

    bool inCheckNow = false;
    if ((unsigned)plyCtx < 256u && inCheckCacheValid[plyCtx]) {
        inCheckNow = inCheckCache[plyCtx];
    } else {
        inCheckNow = attacks::in_check(pos, us);
        if ((unsigned)plyCtx < 256u) {
            inCheckCache[plyCtx] = inCheckNow;
            inCheckCacheValid[plyCtx] = true;
        }
    }

    bool susPin = false;
    bool fastProxy = false;

    if (cs) {
        const int ksq = pos.king_square(us);
        if (ksq >= 0) {
            const int df = std::abs(file_of(from) - file_of(ksq));
            const int dr = std::abs(rank_of(from) - rank_of(ksq));
            susPin = (df == 0 || dr == 0 || df == dr);
        }

        fastProxy = (!inCheckNow && !isKing && !isEp && !isCastle && !susPin);
        if (fastProxy)
            ss.legFast++;
        else
            ss.legSlow++;

        if (isKing)
            ss.legKing++;
        if (susPin)
            ss.legSuspin++;
        else
            ss.legNonsuspin++;
    }

    if (isCastle) {
        if (!movegen::legal_castle_path_ok(pos, m)) {
            if (cs) {
                ss.legFail++;
                if (isKing)
                    ss.legfKing++;
                if (susPin)
                    ss.legfSuspin++;
                else
                    ss.legfNonsuspin++;
                if (isEp)
                    ss.legfEp++;
                else if (isCap)
                    ss.legfCapture++;
                else
                    ss.legfQuiet++;
            }
            return false;
        }
    }

    if (!inCheckNow && !isKing && !isEp && !isCastle) {
        const uint64_t pinned = pinned_mask_for_ply(pos, us, plyCtx);
        if ((pinned & (1ULL << from)) == 0ULL) {
            if (cs) {
                ss.legFast2++;
                if (isCap)
                    ss.legCapture++;
                else
                    ss.legQuiet++;
            }
            return true;
        }
    }

    Undo u = do_move_counted(pos, m);
    const bool ok = !attacks::in_check(pos, us);

    bool givesCheck = false;
    if (cs)
        givesCheck = attacks::in_check(pos, pos.side);

    pos.undo_move(m, u);

    if (cs) {
        if (isEp)
            ss.legEp++;
        else if (givesCheck)
            ss.legCheck++;
        else if (isCap)
            ss.legCapture++;
        else
            ss.legQuiet++;

        if (!ok) {
            ss.legFail++;
            if (isKing)
                ss.legfKing++;
            if (susPin)
                ss.legfSuspin++;
            else
                ss.legfNonsuspin++;

            if (isEp)
                ss.legfEp++;
            else if (givesCheck)
                ss.legfCheck++;
            else if (isCap)
                ss.legfCapture++;
            else
                ss.legfQuiet++;
        }
    }

    return ok;
}

void Searcher::follow_tt_pv(Position& pos, int maxLen, PVLine& out) {
    out.len = 0;
    if (maxLen <= 0)
        return;

    Undo undos[128];
    Move um[128];
    int ucnt = 0;

    uint64_t seen[128];
    int seenN = 0;

    auto seen_before = [&](uint64_t k) {
        for (int i = 0; i < seenN; i++) {
            if (seen[i] == k)
                return true;
        }
        return false;
    };

    Move prev = 0;

    for (int i = 0; i < maxLen && out.len < 128; i++) {
        const uint64_t k = pos.zobKey;

        if (seen_before(k))
            break;
        if (seenN < 128)
            seen[seenN++] = k;

        TTEntry te{};
        if (!stt->probe_copy(k, te))
            break;

        Move m = te.best;
        if (!m)
            break;

        if (!is_legal_move_here(pos, m))
            break;

        if (prev && from_sq(m) == to_sq(prev) && to_sq(m) == from_sq(prev) &&
            promo_of(m) == 0 && promo_of(prev) == 0) {
            break;
        }

        um[ucnt] = m;
        undos[ucnt] = do_move_counted(pos, m);
        ucnt++;

        if (attacks::in_check(pos, flip_color(pos.side))) {
            pos.undo_move(m, undos[--ucnt]);
            break;
        }

        out.m[out.len++] = m;
        prev = m;

        if (stop_or_time_up(false))
            break;
    }

    for (int i = ucnt - 1; i >= 0; i--)
        pos.undo_move(um[i], undos[i]);
}

Searcher::PVLine Searcher::sanitize_pv_from_root(const Position& root, const PVLine& raw, int maxLen) {
    PVLine clean;
    clean.len = 0;

    if (raw.len <= 0 || maxLen <= 0)
        return clean;

    Position cur = root;
    const int lim = std::min({raw.len, maxLen, 128});

    for (int i = 0; i < lim; i++) {
        Move m = raw.m[i];
        if (!m)
            break;
        if (!is_legal_move_here(cur, m))
            break;

        clean.m[clean.len++] = m;
        Undo u = do_move_counted(cur, m);
        (void)u;
    }

    return clean;
}

int Searcher::negamax(Position& pos, int depth, int alpha, int beta, int ply, int prevFrom, int prevTo, int lastTo,
                      bool lastWasCap, PVLine& pv) {
    pv.len = 0;
    const bool pvNode = (beta - alpha > 1);

    if (stop_or_time_up(false))
        return alpha;

    add_node();
    selDepthLocal = std::max(selDepthLocal, ply);

    if (collect_stats()) {
        ss.abNodes++;
        ss.nodePly[depth_bin64(ply)]++;
    }

    if (ply >= 128)
        return eval::evaluate(pos);

    const Color us = pos.side;
    const bool inCheck = attacks::in_check(pos, us);

    if ((unsigned)ply < 256u) {
        pinnedMaskValid[ply] = false;
        inCheckCache[ply] = inCheck;
        inCheckCacheValid[ply] = true;
    }

    alpha = std::max(alpha, -MATE + ply);
    beta = std::min(beta, MATE - ply - 1);
    if (alpha >= beta)
        return alpha;

    const uint64_t key = pos.zobKey;

    if (ply > 0) {
        for (int i = keyPly - 2; i >= 0; i -= 2) {
            if (keyStack[i] == key)
                return 0;
        }
    }

    keyStack[keyPly++] = key;
    struct KeyPop {
        int& p;
        KeyPop(int& x) : p(x) {}
        ~KeyPop() { p--; }
    } _kp(keyPly);

    TTEntry te{};
    if (collect_stats())
        ss.ttProbe++;

    bool ttHit = stt->probe_copy(key, te);
    if (collect_stats() && ttHit)
        ss.ttHit++;

    Move ttMove = 0;
    if (ttHit) {
        ttMove = te.best;

        if (te.depth >= depth) {
            int ttScore = from_tt_score((int)te.score, ply);
            const bool allowTTCut = (!pvNode || !g_params.ttPvConservative || te.flag == TT_EXACT);

            if (te.flag == TT_EXACT && allowTTCut) {
                if (collect_stats())
                    ss.ttCut++;
                return ttScore;
            }

            if (te.flag == TT_ALPHA && allowTTCut && ttScore <= alpha) {
                if (collect_stats())
                    ss.ttCut++;
                return alpha;
            }

            if (te.flag == TT_BETA && allowTTCut && ttScore >= beta) {
                if (collect_stats())
                    ss.ttCut++;
                return beta;
            }
        }
    }

    if (inCheck)
        depth++;

    if (depth <= 0)
        return qsearch(pos, alpha, beta, ply, lastTo, lastWasCap);

    int staticEval = inCheck ? -INF : eval::evaluate(pos);
    if (!inCheck)
        staticEvalStack[ply] = staticEval;
    else if (ply > 0)
        staticEvalStack[ply] = staticEvalStack[ply - 1];
    else
        staticEvalStack[ply] = staticEval;

    bool improving = false;
    if (!inCheck && ply >= 2 && staticEvalStack[ply - 2] > -INF / 2)
        improving = staticEval > staticEvalStack[ply - 2];

    if (g_params.enableRazoring && !inCheck && ply > 0 && depth <= g_params.razorDepthMax) {
        const int razorMargin = razor_margin(depth, improving);
        if (staticEval + razorMargin <= alpha) {
            ps.razorPrune++;
            return qsearch(pos, alpha, beta, ply, lastTo, lastWasCap);
        }
    }

    if (g_params.enableRfp && !inCheck && depth <= g_params.rfpDepthMax && ply > 0) {
        const int rfpMargin = rfp_margin(depth, improving);
        if (staticEval - rfpMargin >= beta) {
            ps.rfpPrune++;
            return beta;
        }
    }

    if (g_params.enableIIR && !inCheck && ply > 0 && !pvNode && !ttHit && depth >= g_params.iirMinDepth) {
        depth = std::max(1, depth - g_params.iirReduce);
        ps.iirApplied++;
    }

    int nTypeIdx = 2;
    if (collect_stats()) {
        NodeContext ctx = make_node_context(pos, depth, alpha, beta, ply, inCheck,
                                            staticEval, improving, ttHit, te);
        if (ctx.nodeType == NodeContext::PV)
            ss.nodePv++;
        else if (ctx.nodeType == NodeContext::CUT)
            ss.nodeCut++;
        else
            ss.nodeAll++;
        nTypeIdx = node_type_index(ctx.nodeType);
        ss.nodeByType[nTypeIdx]++;
    }

    if (g_params.enableNullMove && !inCheck && depth >= g_params.nullMinDepth && ply > 0) {
        if (has_non_pawn_material(pos, us)) {
            int R = g_params.nullBase + depth / g_params.nullDepthDiv;
            R = std::min(R, depth - 1);

            if (beta < MATE - g_params.nullMateGuard && alpha > -MATE + g_params.nullMateGuard) {
                if (collect_stats()) {
                    ss.nullTried++;
                    ss.nullTriedByDepth[depth_bin64(depth)]++;
                }

                NullMoveUndo nu;
                do_null_move(pos, nu);

                PVLine npv;
                int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1,
                                     -1, -1, -1, false, npv);

                undo_null_move(pos, nu);

                if (stop_or_time_up(false))
                    return alpha;

                if (score >= beta) {
                    if (collect_stats()) {
                        ss.nullCut++;
                        ss.nullCutByDepth[depth_bin64(depth)]++;
                    }
                    return beta;
                }
            }
        }
    }

    auto& moves = plyMoves[ply];
    auto& scores = plyScores[ply];

    movegen::generate_pseudo_legal(pos, moves);

    if (moves.empty()) {
        if (inCheck)
            return -MATE + ply;
        return 0;
    }

    scores.resize(moves.size());
    for (int i = 0; i < (int)moves.size(); i++)
        scores[i] = move_score(pos, moves[i], ttMove, ply, prevFrom, prevTo);

    int K = std::min<int>(g_params.nodeOrderK, (int)moves.size());

    for (int i = 0; i < K; i++) {
        int bi = i;
        int bs = scores[i];
        for (int j = i + 1; j < (int)moves.size(); j++) {
            int sj = scores[j];
            if (sj > bs) {
                bs = sj;
                bi = j;
            }
        }
        if (bi != i) {
            std::swap(moves[i], moves[bi]);
            std::swap(scores[i], scores[bi]);
        }
    }

    auto& order = plyOrder[ply];
    order.clear();
    order.reserve(moves.size());

    for (int i = 0; i < (int)moves.size(); i++) {
        Move m = moves[i];
        const bool capLike = is_capture(pos, m) || (flags_of(m) & MF_EP);
        const bool isPromo = (promo_of(m) != 0);
        const bool capBad = (capLike && !isPromo && scores[i] < 50000000);
        if (!capBad)
            order.push_back(i);
    }

    for (int i = 0; i < (int)moves.size(); i++) {
        Move m = moves[i];
        const bool capLike = is_capture(pos, m) || (flags_of(m) & MF_EP);
        const bool isPromo = (promo_of(m) != 0);
        const bool capBad = (capLike && !isPromo && scores[i] < 50000000);
        if (capBad)
            order.push_back(i);
    }

    int bestScore = -INF;
    Move bestMove = 0;
    int bestBucket = MB_QUIET;
    const int origAlpha = alpha;

    PVLine bestPV;
    bestPV.len = 0;

    int legalMovesSearched = 0;
    int quietMovesSearched = 0;
    if (collect_stats())
        ss.moveLoopNodes++;

    bool ttAvail = false;
    bool ttFirstAccounted = false;
    if (collect_stats() && ttHit && ttMove) {
        ttAvail = true;
        ss.ttMoveAvail++;
    }

    for (int oi = 0; oi < (int)order.size(); oi++) {
        if (stop_or_time_up(false))
            return alpha;

        const int kk = order[oi];
        Move m = moves[kk];
        const int curFrom = from_sq(m);
        const int curTo = to_sq(m);

        const bool isCap = is_capture(pos, m) || (flags_of(m) & MF_EP);
        const bool isPromo = (promo_of(m) != 0);
        const bool isQuiet = (!isCap && !isPromo);
        const int bucket = collect_stats()
                               ? classify_bucket(pos, m, ttMove, ply, prevFrom, prevTo, scores[kk], true)
                               : MB_QUIET;

        if (g_params.enableQuietFutility && !inCheck && isQuiet && ply > 0 &&
            depth <= g_params.quietFutilityDepthMax && m != ttMove) {
            const int futMargin = quiet_futility_margin(depth, improving);
            if (staticEval + futMargin <= alpha) {
                ps.quietFutility++;
                if (collect_stats()) {
                    ss.futilitySkip++;
                    ss.futByDepth[depth_bin64(depth)]++;
                }
                continue;
            }
        }

        if (g_params.enableQuietLimit && !inCheck && isQuiet && ply > 0 &&
            depth <= g_params.quietLimitDepthMax && m != ttMove) {
            const int limit = quiet_limit_for_depth(depth);
            if (quietMovesSearched >= limit) {
                ps.quietLimit++;
                if (collect_stats()) {
                    ss.lmpSkip++;
                    ss.lmpByDepth[depth_bin64(depth)]++;
                }
                continue;
            }
        }

        if (g_params.enableCapSeePrune && !inCheck && isCap && !isPromo && ply > 0 &&
            depth <= g_params.capSeeDepthMax && m != ttMove) {
            int sQ = see_quick_main(pos, m);
            if (collect_stats())
                ss.bucketSee[nTypeIdx][bucket]++;

            if (sQ < g_params.capSeeQuickFullTrigger) {
                if (!see_fast_non_negative(pos, m, false)) {
                    int sF = see_full_main(pos, m);
                    if (collect_stats())
                        ss.bucketSee[nTypeIdx][bucket]++;
                    if (sF < g_params.capSeeFullCut) {
                        ps.capSeePrune++;
                        continue;
                    }
                }
            } else if (sQ < g_params.capSeeQuickCut) {
                ps.capSeePrune++;
                continue;
            }
        }

        if (collect_stats())
            ss.bucketLeg[nTypeIdx][bucket]++;

        Undo u = do_move_counted(pos, m);

        if (collect_stats())
            ss.bucketMk[nTypeIdx][bucket]++;

        if (attacks::in_check(pos, us)) {
            pos.undo_move(m, u);
            continue;
        }

        legalMovesSearched++;
        if (collect_stats())
            ss.totalLegalTried++;
        if (collect_stats())
            ss.legalByType[nTypeIdx]++;
        if (collect_stats())
            ss.bucketTry[nTypeIdx][bucket]++;

        if (collect_stats() && ttAvail && !ttFirstAccounted && legalMovesSearched == 1) {
            if (m == ttMove)
                ss.ttMoveFirst++;
            ttFirstAccounted = true;
        }

        if (isQuiet)
            quietMovesSearched++;
        if (collect_stats() && isQuiet)
            ss.quietIdxHist[quiet_idx_bin(legalMovesSearched)]++;

        const int nextLastTo = to_sq(m);
        const bool nextLastWasCap = isCap;

        int score;
        PVLine childPV;

        if (legalMovesSearched == 1) {
            if (collect_stats())
                ss.firstMoveTried++;
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1,
                             curFrom, curTo, nextLastTo, nextLastWasCap, childPV);
        } else {
            int reduction = 0;
            if (ply > 0) {
                reduction = compute_lmr_reduction(depth, legalMovesSearched, inCheck, isQuiet,
                                                  improving, pvNode, us, curFrom, curTo);
                if (reduction > 0) {
                    ps.lmrApplied++;
                    if (collect_stats()) {
                        ss.lmrTried++;
                        ss.lmrByDepth[depth_bin64(depth)]++;

                        bool isK = (m == killer[0][std::min(ply, 127)] ||
                                    m == killer[1][std::min(ply, 127)]);
                        bool isC = ((unsigned)prevFrom < 64u && (unsigned)prevTo < 64u &&
                                    m == countermove[prevFrom][prevTo]);
                        const bool recapture = (lastWasCap && lastTo >= 0 && curTo == lastTo);
                        const bool givesCheck = attacks::in_check(pos, pos.side);
                        const int qh = quiet_bucket_score(us, curFrom, curTo, prevFrom, prevTo);

                        ss.lmrReducedByBucket[lmr_bucket_refined(isK, isC, recapture, givesCheck, qh)]++;
                    }
                }
            }

            int rd = depth - 1 - reduction;
            if (rd < 0)
                rd = 0;

            score = -negamax(pos, rd, -alpha - 1, -alpha, ply + 1,
                             curFrom, curTo, nextLastTo, nextLastWasCap, childPV);

            if (score > alpha && reduction > 0 && rd != depth - 1) {
                if (collect_stats()) {
                    ss.lmrResearched++;
                    ss.lmrReByDepth[depth_bin64(depth)]++;

                    bool isK = (m == killer[0][std::min(ply, 127)] ||
                                m == killer[1][std::min(ply, 127)]);
                    bool isC = ((unsigned)prevFrom < 64u && (unsigned)prevTo < 64u &&
                                m == countermove[prevFrom][prevTo]);
                    const bool recapture = (lastWasCap && lastTo >= 0 && curTo == lastTo);
                    const bool givesCheck = attacks::in_check(pos, pos.side);
                    const int qh = quiet_bucket_score(us, curFrom, curTo, prevFrom, prevTo);

                    ss.lmrResearchedByBucket[lmr_bucket_refined(isK, isC, recapture, givesCheck, qh)]++;
                }

                PVLine childPV2;
                score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1,
                                 curFrom, curTo, nextLastTo, nextLastWasCap, childPV2);
                childPV = childPV2;
            }

            if (score > alpha && score < beta) {
                PVLine childPV3;
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1,
                                 curFrom, curTo, nextLastTo, nextLastWasCap, childPV3);
                childPV = childPV3;
            }
        }

        pos.undo_move(m, u);

        if (stop_or_time_up(false))
            return alpha;

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
            bestBucket = bucket;

            bestPV.m[0] = m;
            bestPV.len = std::min(127, childPV.len + 1);
            for (int k = 0; k < childPV.len && k + 1 < 128; k++)
                bestPV.m[k + 1] = childPV.m[k];
        }

        if (score > alpha)
            alpha = score;
        else if (isQuiet && ply < 128)
            update_quiet_history(us, prevFrom, prevTo, curFrom, curTo, depth, false);

        if (alpha >= beta) {
            ps.betaCutoff++;
            if (collect_stats())
                ss.bucketFh[nTypeIdx][bucket]++;

            if (collect_stats()) {
                if (legalMovesSearched == 1)
                    ss.firstMoveFailHigh++;
            }

            if (isQuiet && ply < 128) {
                if (killer[0][ply] != m) {
                    killer[1][ply] = killer[0][ply];
                    killer[0][ply] = m;
                }

                update_quiet_history(us, prevFrom, prevTo, curFrom, curTo, depth, true);

                if ((unsigned)prevFrom < 64u && (unsigned)prevTo < 64u)
                    countermove[prevFrom][prevTo] = m;
            }
            break;
        }
    }

    if (legalMovesSearched == 0)
        return inCheck ? (-MATE + ply) : 0;

    if (collect_stats())
        ss.bucketBest[nTypeIdx][bestBucket]++;

    pv = bestPV;

    {
        TTFlag flag = TT_EXACT;
        if (bestScore <= origAlpha)
            flag = TT_ALPHA;
        else if (bestScore >= beta)
            flag = TT_BETA;

        int store = clampi(bestScore, -INF, INF);
        store = to_tt_score(store, ply);
        stt->store(key, bestMove, (int16_t)store, (int16_t)depth, (uint8_t)flag);
    }

    if (collect_stats()) {
        if (ttHit && bestMove == ttMove && bestMove)
            ss.ttBest++;
    }

    return bestScore;
}

} // namespace search