#pragma once
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <cmath>
#include <atomic>
#include <thread>
#include <memory>
#include <tuple>
#include <mutex>
#include <shared_mutex>
#include <cstring>

#include "Search.h"
#include "ZobristTables.h"
#include "MoveGeneration.h"
#include "Attack.h"
#include "Evaluation.h"
#include "TT.h"
#include "see_full.h"

namespace search {

#ifndef EV_PV_DEBUG
#define EV_PV_DEBUG 0
#endif

#ifndef EV_LMR_PROFILE
#define EV_LMR_PROFILE 2
#endif

#if EV_PV_DEBUG
#define EV_PV_ASSERT(cond, msg)                                                                                          \
    do {                                                                                                                 \
        if (!(cond)) {                                                                                                   \
            std::cout << "info string pvdbg assert " << (msg) << "\n";                                                  \
            std::cout.flush();                                                                                           \
        }                                                                                                                \
    } while (0)
#else
#define EV_PV_ASSERT(cond, msg) ((void)0)
#endif

inline bool is_white_piece_s(Piece p) {
    int v = (int)p;
    return v >= 1 && v <= 6;
}
inline bool is_black_piece_s(Piece p) {
    int v = (int)p;
    return v >= 9 && v <= 14;
}

int compute_think_time_ms(int mytime_ms, int myinc_ms, int movestogo);
bool move_sane_basic(const Position& pos, Move m);

struct SearchParams {
    int nodeOrderK = 10;
    int rootOrderK = 8;

    bool ttPvConservative = true;

    bool enableRazoring = true;
    int razorDepthMax = 2;
    int razorMarginD1 = 220;
    int razorMarginD2 = 320;
    int razorImprovingBonus = 20;

    bool enableRfp = true;
    int rfpDepthMax = 3;
    int rfpBase = 120;
    int rfpPerDepth = 90;
    int rfpImprovingBonus = 40;

    bool enableIIR = true;
    int iirMinDepth = 6;
    int iirReduce = 1;

    bool enableNullMove = true;
    int nullMinDepth = 3;
    int nullBase = 3;
    int nullDepthDiv = 6;
    int nullMateGuard = 256;

    bool enableQuietFutility = true;
    int quietFutilityDepthMax = 2;
    int quietFutilityD1 = 190;
    int quietFutilityD2 = 290;
    int quietFutilityImprovingBonus = 40;

    bool enableQuietLimit = true;
    int quietLimitDepthMax = 2;
    int quietLimitD1 = 5;
    int quietLimitD2 = 8;

    bool enableCapSeePrune = true;
    int capSeeDepthMax = 4;
    int capSeeQuickFullTrigger = -200;
    int capSeeFullCut = -120;
    int capSeeQuickCut = -120;

    bool enableLmr = true;
    int lmrMinDepth = 3;
    int lmrMove1 = 4;
    int lmrMove2 = 10;
    int lmrMove3 = 14;
    int lmrDepthForMove3 = 7;
    int lmrHistoryLow = 2000;
    int lmrHistoryHigh = 60000;
    int lmrBucketHigh = 6000;
};

extern SearchParams g_params;
extern std::atomic<bool> g_collect_stats;

struct TTPerfStats {
    std::atomic<uint64_t> probeCalls{0};
    std::atomic<uint64_t> probeHits{0};
    std::atomic<uint64_t> probeMiss{0};
    std::atomic<uint64_t> probeLockWaitNs{0};

    std::atomic<uint64_t> storeCalls{0};
    std::atomic<uint64_t> storeWrites{0};
    std::atomic<uint64_t> storeSkips{0};
    std::atomic<uint64_t> storeLockWaitNs{0};

    std::atomic<uint64_t> hashfullCalls{0};
    std::atomic<uint64_t> hashfullWaitNs{0};

    void clear();
};

extern TTPerfStats g_ttperf;

inline Color flip_color(Color c) {
    return (c == WHITE) ? BLACK : WHITE;
}
inline int color_index(Color c) {
    return (c == WHITE) ? 0 : 1;
}

extern std::atomic<bool> g_stop;
extern std::atomic<int64_t> g_endTimeMs;
extern std::atomic<uint64_t> g_nodes_total;
extern std::atomic<uint64_t> g_nodes_limit;
extern std::atomic<int> g_threads;

int64_t now_ms();
uint64_t now_ns();

void start_timer(int movetime_ms, bool infinite);
bool time_up();

inline int clampi(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

void print_score_uci(int score);

int to_tt_score(int s, int ply);
int from_tt_score(int s, int ply);

inline char file_char(int f) {
    return char('a' + f);
}
inline char rank_char(int r) {
    return char('1' + r);
}

int hashfull_permille_fallback(const TT& tt);

struct SharedTT {
    TT tt;

    static constexpr int LOCKS = 4096;
    std::vector<std::shared_mutex> locks;
    mutable std::shared_mutex table_mtx;

    SharedTT();

    int lock_index(uint64_t key) const;

    static uint64_t pack_payload(Move best, int16_t score, int8_t depth, uint8_t flag);
    static void unpack_payload(uint64_t p, Move& best, int16_t& score, int8_t& depth, uint8_t& flag);

    bool probe_copy(uint64_t key, TTEntry& out);
    void store(uint64_t key, Move best, int16_t score, int16_t depth, uint8_t flag);

    void resize_mb(int mb);
    void clear();
    int hashfull_permille() const;
};

struct Searcher {
    SharedTT* stt = nullptr;

    Move killer[2][128]{};
    int history[2][64][64]{};

    Move countermove[64][64]{};
    int contHist[2][64][64][64][64]{};

    uint64_t nodes = 0;
    int selDepthLocal = 0;

    struct PruneStats {
        uint64_t razorPrune = 0;
        uint64_t rfpPrune = 0;
        uint64_t quietFutility = 0;
        uint64_t quietLimit = 0;
        uint64_t capSeePrune = 0;
        uint64_t iirApplied = 0;
        uint64_t lmrApplied = 0;
        uint64_t betaCutoff = 0;
        inline void clear() { *this = PruneStats{}; }
    } ps;

    struct SearchStats {
        static constexpr int BUCKET_N = 5;
        uint64_t nodePv = 0;
        uint64_t nodeCut = 0;
        uint64_t nodeAll = 0;
        uint64_t nodeByType[3]{};
        uint64_t legalByType[3]{};
        uint64_t ttProbe = 0;
        uint64_t ttHit = 0;
        uint64_t ttCut = 0;
        uint64_t ttBest = 0;
        uint64_t ttMoveAvail = 0;
        uint64_t ttMoveFirst = 0;
        uint64_t firstMoveTried = 0;
        uint64_t firstMoveFailHigh = 0;
        uint64_t lmrTried = 0;
        uint64_t lmrResearched = 0;
        uint64_t lmrReducedByBucket[4]{};
        uint64_t lmrResearchedByBucket[4]{};
        uint64_t nullTried = 0;
        uint64_t nullCut = 0;
        uint64_t nullVerifyTried = 0;
        uint64_t nullVerifyFail = 0;
        uint64_t lmpSkip = 0;
        uint64_t futilitySkip = 0;
        uint64_t totalLegalTried = 0;
        uint64_t moveLoopNodes = 0;
        uint64_t rootIters = 0;
        uint64_t rootFirstBestOrCut = 0;
        uint64_t rootBestSrc[5]{};
        uint64_t rootPvsReSearch = 0;
        uint64_t rootLmrReSearch = 0;
        uint64_t rootNonFirstTried = 0;
        uint64_t aspFail = 0;
        uint64_t proxyReversalAfterNull = 0;
        uint64_t proxyReversalAfterRfp = 0;
        uint64_t proxyReversalAfterRazor = 0;
        uint64_t timeChecks = 0;
        uint64_t legCalls = 0;
        uint64_t legFail = 0;
        uint64_t legQuiet = 0;
        uint64_t legfQuiet = 0;
        uint64_t legCapture = 0;
        uint64_t legfCapture = 0;
        uint64_t legCheck = 0;
        uint64_t legfCheck = 0;
        uint64_t legEp = 0;
        uint64_t legfEp = 0;
        uint64_t legKing = 0;
        uint64_t legfKing = 0;
        uint64_t legSuspin = 0;
        uint64_t legfSuspin = 0;
        uint64_t legNonsuspin = 0;
        uint64_t legfNonsuspin = 0;
        uint64_t legFast = 0;
        uint64_t legSlow = 0;
        uint64_t legFast2 = 0;
        uint64_t pinCalc = 0;
        uint64_t seeCallsMain = 0;
        uint64_t seeCallsQ = 0;
        uint64_t seeFastSafe = 0;
        uint64_t makeCalls = 0;
        uint64_t makeMain = 0;
        uint64_t makeQ = 0;
        uint64_t nodeBatchFlush = 0;
        uint64_t nodeBatchFlushNs = 0;
        uint64_t nodeBatchCapCheck = 0;
        uint64_t abNodes = 0;
        uint64_t qNodes = 0;
        uint64_t nodePly[64]{};
        uint64_t qNodePly[64]{};
        uint64_t lmrByDepth[64]{};
        uint64_t lmrReByDepth[64]{};
        uint64_t nullTriedByDepth[64]{};
        uint64_t nullCutByDepth[64]{};
        uint64_t lmpByDepth[64]{};
        uint64_t futByDepth[64]{};
        uint64_t quietIdxHist[8]{};
        uint64_t pvInfoCount = 0;
        uint64_t pvLenSum = 0;
        uint64_t pvLenMin = 0;
        uint64_t pvLenMax = 0;
        uint64_t pvLenLe2 = 0;
        uint64_t pvLenLe4 = 0;
        uint64_t pvLenDepthCnt[64]{};
        uint64_t pvLenDepthSum[64]{};
        uint64_t tmStopHard = 0;
        uint64_t tmStopSoft = 0;
        uint64_t tmStopBestStable = 0;
        uint64_t tmStopIterFinished = 0;
        uint64_t tmStopAsp = 0;
        uint64_t tmStopOther = 0;
        uint64_t tmStopBeforeNextDepth = 0;
        uint64_t tmIterMsLast3[3]{};
        uint64_t tmIterBmChangedLast3[3]{};
        uint64_t tmIterScChangedLast3[3]{};
        uint64_t bucketTry[3][BUCKET_N]{};
        uint64_t bucketFh[3][BUCKET_N]{};
        uint64_t bucketBest[3][BUCKET_N]{};
        uint64_t bucketSee[3][BUCKET_N]{};
        uint64_t bucketLeg[3][BUCKET_N]{};
        uint64_t bucketMk[3][BUCKET_N]{};
        inline void clear() { *this = SearchStats{}; }
    } ss;

    struct NodeContext {
        enum NodeType : uint8_t { PV = 0, CUT = 1, ALL = 2 };
        NodeType nodeType = PV;
        int depth = 0;
        int ply = 0;
        bool inCheck = false;
        int staticEval = -INF;
        bool improving = false;
        bool ttHit = false;
        int ttDepth = -1;
        uint8_t ttBound = TT_ALPHA;
        uint8_t ttConfidence = 0;
        uint8_t endgameRisk = 0;
    };

    uint64_t nodes_batch = 0;
    bool canSignalGlobalStop = true;
    enum : uint8_t { TM_CAUSE_NONE = 0, TM_CAUSE_HARD = 1, TM_CAUSE_OTHER = 2 };
    uint8_t tmLastCause = TM_CAUSE_NONE;
    static constexpr uint64_t NODE_BATCH = 4096;
    uint32_t time_check_tick = 0;
    static constexpr uint32_t TIME_CHECK_MASK_NODE = 4095;
    static constexpr uint32_t TIME_CHECK_MASK_ROOT = 255;

    struct PVLine {
        Move m[128]{};
        int len = 0;
    };

    Searcher();
    void bind(SharedTT* shared);

    inline uint64_t key_of(const Position& pos) const { return pos.zobKey; }

    uint64_t keyStack[256]{};
    int keyPly = 0;
    int staticEvalStack[256]{};
    uint64_t pinnedMaskCache[256]{};
    bool pinnedMaskValid[256]{};
    int kingSqCache[256]{};
    bool inCheckCache[256]{};
    bool inCheckCacheValid[256]{};

    static constexpr int MAX_PLY = 128;

    std::vector<Move> plyMoves[MAX_PLY];
    std::vector<int> plyScores[MAX_PLY];
    std::vector<int> plyOrder[MAX_PLY];

    struct QNode {
        Move m;
        int key;
        bool cap;
        bool promo;
        bool check;
    };
    std::vector<QNode> plyQList[MAX_PLY];

    inline void update_stat(int& v, int bonus, int cap = 300000) {
        bonus = clampi(bonus, -cap, cap);
        v += bonus - int((1LL * std::abs(bonus) * v) / cap);
        v = clampi(v, -cap, cap);
    }

    inline bool is_capture(const Position& pos, Move m) const {
        if (flags_of(m) & MF_EP)
            return true;
        int to = to_sq(m);
        return pos.board[to] != NO_PIECE;
    }

    inline int mvv_lva(Piece victim, Piece attacker) {
        static const int V[7] = {0, 100, 320, 330, 500, 900, 0};
        int vv = (victim == NO_PIECE) ? 0 : V[type_of(victim)];
        int aa = (attacker == NO_PIECE) ? 0 : V[type_of(attacker)];
        return vv * 10 - aa;
    }

    inline int see_quick(const Position& pos, Move m) {
        int from = from_sq(m);
        int to = to_sq(m);

        Piece a = pos.board[from];
        Piece v = pos.board[to];

        static const int V[7] = {0, 100, 320, 330, 500, 900, 0};

        int av = V[type_of(a)];
        int vv = (v == NO_PIECE ? 0 : V[type_of(v)]);

        if (flags_of(m) & MF_EP)
            vv = 100;
        if (promo_of(m))
            vv += 800;

        return vv - av;
    }

    inline int razor_margin(int depth, bool improving) const {
        int margin = (depth <= 1) ? g_params.razorMarginD1 : g_params.razorMarginD2;
        if (improving)
            margin += g_params.razorImprovingBonus;
        return margin;
    }

    inline int rfp_margin(int depth, bool improving) const {
        int margin = g_params.rfpBase + g_params.rfpPerDepth * depth;
        if (improving)
            margin += g_params.rfpImprovingBonus;
        return margin;
    }

    inline int quiet_futility_margin(int depth, bool improving) const {
        int margin = (depth <= 1) ? g_params.quietFutilityD1 : g_params.quietFutilityD2;
        if (improving)
            margin += g_params.quietFutilityImprovingBonus;
        return margin;
    }

    inline int quiet_limit_for_depth(int depth) const {
        return (depth <= 1) ? g_params.quietLimitD1 : g_params.quietLimitD2;
    }

    void update_quiet_history(Color us, int prevFrom, int prevTo, int from, int to, int depth, bool good);
    int compute_lmr_reduction(int depth, int legalMovesSearched, bool inCheck, bool isQuiet, bool improving,
                              bool isPvNode, Color us, int from, int to);
    int node_type_index(NodeContext::NodeType t) const;
    int lmr_bucket(bool isKiller, bool isCounter, int quietScore) const;
    int quiet_bucket_score(Color us, int from, int to, int prevFrom, int prevTo) const;
    int lmr_bucket_refined(bool isKiller, bool isCounter, bool recapture, bool givesCheck, int qScore) const;
    int depth_bin64(int d) const;
    int quiet_idx_bin(int idx1Based) const;

    enum MoveBucket : int {
        MB_TT = 0,
        MB_CAP_GOOD = 1,
        MB_QUIET_SPECIAL = 2,
        MB_QUIET = 3,
        MB_CAP_BAD = 4
    };

    int classify_bucket(const Position& pos, Move m, Move ttMove, int ply, int prevFrom, int prevTo, int score,
                        bool scoreKnown) const;

    NodeContext make_node_context(const Position& pos, int depth, int alpha, int beta, int ply, bool inCheck,
                                  int staticEval, bool improving, bool ttHit, const TTEntry& te) const;

    int move_score(const Position& pos, Move m, Move ttMove, int ply, int prevFrom, int prevTo);

    void batch_time_check_soft();
    void flush_nodes_batch();
    bool stop_or_time_up(bool rootNode);
    void add_node();
    Undo do_move_counted(Position& pos, Move m, bool inQ = false);
    int see_quick_main(const Position& pos, Move m);
    int see_full_main(const Position& pos, Move m);
    int see_quick_q(const Position& pos, Move m);
    int see_full_q(const Position& pos, Move m);
    uint64_t compute_pinned_mask_for_side(const Position& pos, Color us);
    uint64_t pinned_mask_for_ply(const Position& pos, Color us, int plyCtx);
    bool see_fast_non_negative(Position& pos, Move m, bool inQ = false);

    struct NullMoveUndo {
        int ep;
        Color side;
        uint64_t key;
    };

    void do_null_move(Position& pos, NullMoveUndo& u);
    void undo_null_move(Position& pos, const NullMoveUndo& u);
    bool has_non_pawn_material(const Position& pos, Color c);
    bool is_legal_move_here(Position& pos, Move m, int plyCtx = -1);
    void follow_tt_pv(Position& pos, int maxLen, PVLine& out);
    PVLine sanitize_pv_from_root(const Position& root, const PVLine& raw, int maxLen = 128);

    int qsearch(Position& pos, int alpha, int beta, int ply, int lastTo, bool lastWasCap);
    int negamax(Position& pos, int depth, int alpha, int beta, int ply, int prevFrom, int prevTo,
                int lastTo, bool lastWasCap, PVLine& pv);
    Result think(Position& pos, const Limits& lim, bool emitInfo);
};

extern int g_hash_mb;
extern std::unique_ptr<SharedTT> g_shared_tt;
extern std::vector<std::unique_ptr<Searcher>> g_pool_owner;
extern std::vector<Searcher*> g_pool;

int threads();
void ensure_pool();

} // namespace search