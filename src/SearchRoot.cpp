#include "SearchCore.h"

namespace search {

Result Searcher::think(Position& pos, const Limits& lim, bool emitInfo) {
    canSignalGlobalStop = emitInfo;
    tmLastCause = TM_CAUSE_NONE;
    const int64_t tmThinkStartMs = now_ms();

    keyPly = 0;
    keyStack[keyPly++] = pos.zobKey;
    for (int i = 0; i < 256; i++) {
        staticEvalStack[i] = -INF;
        pinnedMaskValid[i] = false;
        inCheckCacheValid[i] = false;
    }

    Result res{};
    nodes = 0;
    nodes_batch = 0;
    time_check_tick = 0;
    selDepthLocal = 0;
    ps.clear();
    ss.clear();

    const int maxDepth = (lim.depth > 0 ? lim.depth : 64);
    const int64_t startT = now_ms();
    int lastFlushMs = 0;
    int lastInfoDepth = 0;

    std::vector<Move> rootMoves;
    rootMoves.reserve(256);
    movegen::generate_legal(pos, rootMoves);

    if (rootMoves.empty()) {
        flush_nodes_batch();
        res.bestMove = 0;
        res.ponderMove = 0;
        res.score = 0;
        res.nodes = nodes;
        return res;
    }

    Move bestMove = rootMoves[0];
    int bestScore = -INF;

    constexpr int ASP_START = 35;
    constexpr int PV_MAX = 128;

    auto now_time_nodes_nps = [&]() {
        int t = (int)(now_ms() - startT);
        if (t < 1)
            t = 1;

        uint64_t nodesAll = g_nodes_total.load(std::memory_order_relaxed) + nodes_batch;
        uint64_t npsAll = (nodesAll * 1000ULL) / (uint64_t)t;
        return std::tuple<int, uint64_t, uint64_t>(t, npsAll, nodesAll);
    };

    PVLine rootPV;
    PVLine rootPVLegal;
    rootPV.len = 0;
    rootPVLegal.len = 0;

    int lastCompletedDepth = 0;
    int lastCompletedScore = -INF;
    Move lastCompletedBestMove = 0;
    PVLine lastCompletedPV;
    lastCompletedPV.len = 0;
    bool hasLastCompleted = false;
    bool stoppedMidDepth = false;
    bool stoppedBeforeNextDepth = false;
    int64_t prevIterEndMs = 0;
    Move prevCompletedBestMove = 0;
    int prevCompletedScore = 0;
    bool hasPrevCompleted = false;

    auto same_pv = [&](const PVLine& a, const PVLine& b) {
        if (a.len != b.len)
            return false;
        if (a.len <= 0)
            return true;
        return std::memcmp(a.m, b.m, sizeof(Move) * (size_t)a.len) == 0;
    };

    auto emit_completed_info = [&](int d, int score, const PVLine& pvLine) {
        if (!emitInfo)
            return;

        auto [t, nps, nodesAll] = now_time_nodes_nps();
        rootPVLegal = sanitize_pv_from_root(pos, pvLine, PV_MAX);
        int hashfull = stt->hashfull_permille();
        int sd = std::max(1, selDepthLocal);

        std::cout << "info depth " << d << " seldepth " << sd << " multipv 1 ";
        print_score_uci(score);
        std::cout << " nodes " << nodesAll << " nps " << nps << " hashfull " << hashfull
                  << " tbhits 0 time " << t << " pv ";

        int outN = std::min(PV_MAX, rootPVLegal.len);
        if (collect_stats()) {
            ss.pvInfoCount++;
            ss.pvLenSum += (uint64_t)outN;
            if (ss.pvInfoCount == 1) {
                ss.pvLenMin = (uint64_t)outN;
                ss.pvLenMax = (uint64_t)outN;
            } else {
                ss.pvLenMin = std::min<uint64_t>(ss.pvLenMin, (uint64_t)outN);
                ss.pvLenMax = std::max<uint64_t>(ss.pvLenMax, (uint64_t)outN);
            }
            if (outN <= 2) ss.pvLenLe2++;
            if (outN <= 4) ss.pvLenLe4++;
            int db = depth_bin64(d);
            ss.pvLenDepthCnt[db]++;
            ss.pvLenDepthSum[db] += (uint64_t)outN;
        }

        for (int i = 0; i < outN; i++) {
            Move pm = rootPVLegal.m[i];
            if (!pm)
                break;
            std::cout << move_to_uci(pm) << " ";
        }
        std::cout << "\n";

        int curMs = t;
        if (curMs - lastFlushMs >= 50) {
            std::cout.flush();
            lastFlushMs = curMs;
        }
        lastInfoDepth = d;
    };

    auto classify_root_source = [&](Move m, Move ttM) {
        if (m && ttM && m == ttM)
            return 0;
        const bool cap = is_capture(pos, m) || (flags_of(m) & MF_EP) || promo_of(m);
        if (cap)
            return 1;
        if (m == killer[0][0] || m == killer[1][0])
            return 2;
        return 4;
    };

    auto root_search = [&](int d, int alpha, int beta, Move& outBestMove, int& outBestScore,
                           PVLine& outPV, bool& outOk) {
        outOk = true;

        int curAlpha = alpha;
        int curBeta = beta;

        Move iterBestMove = 0;
        int iterBestScore = -INF;
        PVLine iterPV;
        iterPV.len = 0;
        int iterBestIndex = -1;
        bool firstMoveCut = false;

        int rootLegalsSearched = 0;

        for (int i = 0; i < (int)rootMoves.size(); i++) {
            if (stop_or_time_up(true)) {
                outOk = false;
                break;
            }

            Move m = rootMoves[i];
            const bool isCap = is_capture(pos, m) || (flags_of(m) & MF_EP);
            const bool isPromo = (promo_of(m) != 0);

            Undo u = do_move_counted(pos, m);
            rootLegalsSearched++;

            bool givesCheck = false;
            if (d >= 6 && i >= 4)
                givesCheck = attacks::in_check(pos, pos.side);

            const int nextLastTo = to_sq(m);
            const bool nextLastWasCap = isCap;

            int score = -INF;
            int r = 0;

            if (!isCap && !isPromo && !givesCheck && d >= 6 && i >= 4) {
                r = 1;
                if (d >= 10 && i >= 10)
                    r = 2;
                r = std::min(r, d - 2);
            }

            PVLine childPV;
            const int curFrom = from_sq(m);
            const int curTo = to_sq(m);

            if (rootLegalsSearched == 1) {
                score = -negamax(pos, d - 1, -curBeta, -curAlpha, 1,
                                 curFrom, curTo, nextLastTo, nextLastWasCap, childPV);
            } else {
                if (collect_stats())
                    ss.rootNonFirstTried++;

                int rd = (d - 1) - r;
                if (rd < 0)
                    rd = 0;

                score = -negamax(pos, rd, -curAlpha - 1, -curAlpha, 1,
                                 curFrom, curTo, nextLastTo, nextLastWasCap, childPV);

                if (score > curAlpha && score < curBeta) {
                    if (collect_stats()) {
                        ss.rootPvsReSearch++;
                        if (r > 0)
                            ss.rootLmrReSearch++;
                    }
                    PVLine childPV2;
                    score = -negamax(pos, d - 1, -curBeta, -curAlpha, 1,
                                     curFrom, curTo, nextLastTo, nextLastWasCap, childPV2);
                    childPV = childPV2;
                }
            }

            pos.undo_move(m, u);

            if (stop_or_time_up(true)) {
                outOk = false;
                break;
            }

            if (score > iterBestScore) {
                iterBestScore = score;
                iterBestMove = m;
                iterBestIndex = i;

                iterPV.m[0] = m;
                iterPV.len = std::min(127, childPV.len + 1);
                for (int k = 0; k < childPV.len && k + 1 < 128; k++)
                    iterPV.m[k + 1] = childPV.m[k];

                EV_PV_ASSERT(iterPV.len > 0 && iterPV.m[0] == iterBestMove,
                             "rootPV update without root best");
            }

            if (score > curAlpha)
                curAlpha = score;
            if (curAlpha >= curBeta) {
                if (i == 0)
                    firstMoveCut = true;
                break;
            }
        }

        if (rootLegalsSearched == 0)
            outOk = false;

        if (collect_stats() && rootLegalsSearched > 0) {
            ss.rootIters++;
            if (iterBestIndex == 0 || firstMoveCut)
                ss.rootFirstBestOrCut++;
        }

        outBestMove = iterBestMove;
        outBestScore = iterBestScore;
        outPV = iterPV;
    };

    Move prevIterBestMove = 0;
    int prevIterScore = 0;
    bool prevHadNull = false;
    bool prevHadRfp = false;
    bool prevHadRazor = false;

    for (int d = 1; d <= maxDepth; d++) {
        if (stop_or_time_up(true)) {
            stoppedMidDepth = true;
            stoppedBeforeNextDepth = true;
            if (tmLastCause == TM_CAUSE_HARD) ss.tmStopHard++;
            else ss.tmStopOther++;
            break;
        }

        if (!lim.infinite && lim.movetime_ms >= 2000 && hasLastCompleted && hasPrevCompleted && d >= 10) {
            int64_t end = g_endTimeMs.load(std::memory_order_relaxed);
            int64_t now = now_ms();
            int64_t rem = end - now;
            if (rem > 0) {
                const bool stableNow =
                    (ss.tmIterBmChangedLast3[0] == 0 &&
                     ss.tmIterBmChangedLast3[1] == 0 &&
                     ss.tmIterScChangedLast3[0] == 0);

                if (stableNow) {
                    uint64_t i0 = ss.tmIterMsLast3[0], i1 = ss.tmIterMsLast3[1], i2 = ss.tmIterMsLast3[2];
                    uint64_t pred = i0 ? i0 : (i1 ? i1 : i2);
                    if (i0 && i1)
                        pred = std::max(pred, (i0 + i1) / 2);

                    int thr = g_threads.load(std::memory_order_relaxed);
                    int pct = 55;
                    if (thr >= 12) pct = 25;
                    else if (thr >= 8) pct = 35;

                    if (pred > 0 && rem * 100 < (int64_t)(pred * pct)) {
                        ss.tmStopSoft++;
                        ss.tmStopBestStable++;
                        stoppedMidDepth = true;
                        stoppedBeforeNextDepth = true;
                        break;
                    }
                }
            }
        }

        if (bestMove) {
            auto it = std::find(rootMoves.begin(), rootMoves.end(), bestMove);
            if (it != rootMoves.end())
                std::swap(rootMoves[0], *it);
        }

        std::vector<int> rootScores(rootMoves.size());
        for (int i = 0; i < (int)rootMoves.size(); i++)
            rootScores[i] = move_score(pos, rootMoves[i], bestMove, 0, -1, -1);

        const int K = std::min<int>(g_params.rootOrderK, (int)rootMoves.size());
        for (int i = 0; i < K; i++) {
            int bi = i, bs = rootScores[i];
            for (int j = i + 1; j < (int)rootMoves.size(); j++) {
                if (rootScores[j] > bs) {
                    bs = rootScores[j];
                    bi = j;
                }
            }
            if (bi != i) {
                std::swap(rootMoves[i], rootMoves[bi]);
                std::swap(rootScores[i], rootScores[bi]);
            }
        }

        const bool useAsp = (d > 5 && bestScore > -INF / 2 && bestScore < INF / 2);
        int alpha = useAsp ? (bestScore - ASP_START) : -INF;
        int beta  = useAsp ? (bestScore + ASP_START) : INF;

        Move localBestMove = bestMove;
        int localBestScore = bestScore;
        PVLine localPV;
        localPV.len = 0;
        bool ok = false;
        const PVLine rootPVBeforeDepth = rootPV;

        Move rootTTMove = 0;
        {
            TTEntry rte{};
            if (stt->probe_copy(pos.zobKey, rte) && rte.best && is_legal_move_here(pos, rte.best, 0))
                rootTTMove = rte.best;
        }

        const uint64_t pRazor0 = ps.razorPrune;
        const uint64_t pRfp0   = ps.rfpPrune;
        const uint64_t pNull0  = ss.nullTried;

        root_search(d, alpha, beta, localBestMove, localBestScore, localPV, ok);
        if (!ok) {
            stoppedMidDepth = true;
            if (tmLastCause == TM_CAUSE_HARD) ss.tmStopHard++;
            else ss.tmStopOther++;
            break;
        }

        if (useAsp && (localBestScore <= alpha || localBestScore >= beta)) {
            if (collect_stats())
                ss.aspFail++;

            EV_PV_ASSERT(same_pv(rootPV, rootPVBeforeDepth),
                         "aspiration fail search overwrote rootPV");

            alpha = -INF;
            beta = INF;
            root_search(d, alpha, beta, localBestMove, localBestScore, localPV, ok);
            if (!ok) {
                stoppedMidDepth = true;
                ss.tmStopAsp++;
                if (tmLastCause == TM_CAUSE_HARD) ss.tmStopHard++;
                else ss.tmStopOther++;
                break;
            }
        }

        if (collect_stats()) {
            ss.rootBestSrc[classify_root_source(localBestMove, rootTTMove)]++;

            const bool hadRazor = (ps.razorPrune > pRazor0);
            const bool hadRfp   = (ps.rfpPrune > pRfp0);
            const bool hadNull  = (ss.nullTried > pNull0);

            if (prevIterBestMove && localBestMove && localBestMove != prevIterBestMove &&
                std::abs(localBestScore - prevIterScore) >= 120) {
                if (prevHadNull)  ss.proxyReversalAfterNull++;
                if (prevHadRfp)   ss.proxyReversalAfterRfp++;
                if (prevHadRazor) ss.proxyReversalAfterRazor++;
            }

            prevHadNull = hadNull;
            prevHadRfp = hadRfp;
            prevHadRazor = hadRazor;
            prevIterBestMove = localBestMove;
            prevIterScore = localBestScore;
        }

        bestMove = localBestMove;
        bestScore = localBestScore;
        rootPV = localPV;
        EV_PV_ASSERT(rootPV.len <= 0 || rootPV.m[0] == bestMove,
                     "global rootPV mismatch with bestMove");

        lastCompletedDepth = d;
        lastCompletedScore = bestScore;
        lastCompletedBestMove = bestMove;
        lastCompletedPV = rootPV;
        hasLastCompleted = true;

        {
            int64_t now = now_ms();
            uint64_t iterMs = (uint64_t)std::max<int64_t>(1, now - prevIterEndMs);
            prevIterEndMs = now;

            const bool bmChanged = (!hasPrevCompleted || bestMove != prevCompletedBestMove);
            const bool scChanged = (!hasPrevCompleted || bestScore != prevCompletedScore);

            ss.tmIterMsLast3[2] = ss.tmIterMsLast3[1];
            ss.tmIterMsLast3[1] = ss.tmIterMsLast3[0];
            ss.tmIterMsLast3[0] = iterMs;

            ss.tmIterBmChangedLast3[2] = ss.tmIterBmChangedLast3[1];
            ss.tmIterBmChangedLast3[1] = ss.tmIterBmChangedLast3[0];
            ss.tmIterBmChangedLast3[0] = bmChanged ? 1 : 0;

            ss.tmIterScChangedLast3[2] = ss.tmIterScChangedLast3[1];
            ss.tmIterScChangedLast3[1] = ss.tmIterScChangedLast3[0];
            ss.tmIterScChangedLast3[0] = scChanged ? 1 : 0;

            prevCompletedBestMove = bestMove;
            prevCompletedScore = bestScore;
            hasPrevCompleted = true;
        }

        emit_completed_info(d, bestScore, rootPV);
    }

    flush_nodes_batch();

    if (hasLastCompleted) {
        bestMove = lastCompletedBestMove;
        bestScore = lastCompletedScore;
        rootPV = lastCompletedPV;
    }

    if (stoppedMidDepth && emitInfo && hasLastCompleted && lastInfoDepth < lastCompletedDepth) {
        EV_PV_ASSERT(same_pv(rootPV, lastCompletedPV),
                     "stop path overwrote lastCompletedPV");
        emit_completed_info(lastCompletedDepth, lastCompletedScore, lastCompletedPV);
    }

    if (!stoppedMidDepth && hasLastCompleted && lastCompletedDepth >= maxDepth)
        ss.tmStopIterFinished++;
    if (stoppedBeforeNextDepth)
        ss.tmStopBeforeNextDepth++;

    res.bestMove = bestMove;
    res.score = bestScore;
    res.nodes = nodes;

    rootPVLegal = sanitize_pv_from_root(pos, rootPV, PV_MAX);
    res.ponderMove = (rootPVLegal.len >= 2 ? rootPVLegal.m[1] : 0);

    if (emitInfo) {
        std::cout << "info string prune razor=" << ps.razorPrune
                  << " rfp=" << ps.rfpPrune
                  << " qfut=" << ps.quietFutility
                  << " qlim=" << ps.quietLimit
                  << " csee=" << ps.capSeePrune
                  << " iir=" << ps.iirApplied
                  << " lmr=" << ps.lmrApplied
                  << " bcut=" << ps.betaCutoff << "\n";
    }

    return res;
}

} // namespace search