#include "SearchCore.h"

namespace search {

SearchParams g_params{};
std::atomic<bool> g_collect_stats{false};
TTPerfStats g_ttperf{};

std::atomic<bool> g_stop{false};
std::atomic<int64_t> g_endTimeMs{0};
std::atomic<uint64_t> g_nodes_total{0};
std::atomic<uint64_t> g_nodes_limit{0};
std::atomic<int> g_threads{1};

int g_hash_mb = 64;
std::unique_ptr<SharedTT> g_shared_tt;
std::vector<std::unique_ptr<Searcher>> g_pool_owner;
std::vector<Searcher*> g_pool;

Searcher::Searcher() {
    for (int i = 0; i < MAX_PLY; i++) {
        plyMoves[i].reserve(256);
        plyScores[i].reserve(256);
        plyOrder[i].reserve(256);
        plyQList[i].reserve(256);
    }
}

void Searcher::bind(SharedTT* shared) {
    stt = shared;
}

int compute_think_time_ms(int mytime_ms, int myinc_ms, int movestogo) {
    if (mytime_ms <= 0)
        return -1;

    if (movestogo > 0) {
        int base = mytime_ms / std::max(1, movestogo);
        int inc_part = (myinc_ms * 8) / 10;
        int t = base + inc_part;

        t = std::max(10, t);
        t = std::min(t, mytime_ms / 2);
        return t;
    }

    int t = mytime_ms / 25 + (myinc_ms * 8) / 10;
    t = std::max(10, t);
    t = std::min(t, mytime_ms / 2);
    return t;
}

bool move_sane_basic(const Position& pos, Move m) {
    if (!m)
        return false;

    int from = from_sq(m), to = to_sq(m);
    if ((unsigned)from >= 64u || (unsigned)to >= 64u)
        return false;

    Piece pc = pos.board[from];
    if (pc == NO_PIECE)
        return false;

    if (pos.side == WHITE) {
        if (!is_white_piece_s(pc))
            return false;
    } else {
        if (!is_black_piece_s(pc))
            return false;
    }

    if (!(flags_of(m) & MF_EP)) {
        Piece cap = pos.board[to];
        if (cap != NO_PIECE) {
            if (pos.side == WHITE) {
                if (is_white_piece_s(cap))
                    return false;
            } else {
                if (is_black_piece_s(cap))
                    return false;
            }
        }
    }
    return true;
}

void set_collect_stats(bool on) {
    g_collect_stats.store(on, std::memory_order_relaxed);
}

bool collect_stats() {
    return g_collect_stats.load(std::memory_order_relaxed);
}

void TTPerfStats::clear() {
    probeCalls.store(0, std::memory_order_relaxed);
    probeHits.store(0, std::memory_order_relaxed);
    probeMiss.store(0, std::memory_order_relaxed);
    probeLockWaitNs.store(0, std::memory_order_relaxed);

    storeCalls.store(0, std::memory_order_relaxed);
    storeWrites.store(0, std::memory_order_relaxed);
    storeSkips.store(0, std::memory_order_relaxed);
    storeLockWaitNs.store(0, std::memory_order_relaxed);

    hashfullCalls.store(0, std::memory_order_relaxed);
    hashfullWaitNs.store(0, std::memory_order_relaxed);
}

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint64_t now_ns() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void stop() {
    g_stop.store(true, std::memory_order_relaxed);
}

void start_timer(int movetime_ms, bool infinite) {
    g_stop.store(false, std::memory_order_relaxed);
    if (infinite || movetime_ms <= 0)
        g_endTimeMs.store(0, std::memory_order_relaxed);
    else
        g_endTimeMs.store(now_ms() + (int64_t)movetime_ms, std::memory_order_relaxed);
}

bool time_up() {
    if (g_stop.load(std::memory_order_relaxed))
        return true;
    int64_t end = g_endTimeMs.load(std::memory_order_relaxed);
    if (end == 0)
        return false;
    return now_ms() >= end;
}

void print_score_uci(int score) {
    const int MATE_SCORE = MATE;

    if (std::abs(score) >= MATE_SCORE - 1000) {
        int mateIn = (MATE_SCORE - std::abs(score) + 1) / 2;
        if (score < 0)
            mateIn = -mateIn;
        std::cout << "score mate " << mateIn;
    } else {
        std::cout << "score cp " << score;
    }
}

int to_tt_score(int s, int ply) {
    if (s > MATE - 256)
        return s + ply;
    if (s < -MATE + 256)
        return s - ply;
    return s;
}

int from_tt_score(int s, int ply) {
    if (s > MATE - 256)
        return s - ply;
    if (s < -MATE + 256)
        return s + ply;
    return s;
}

std::string move_to_uci(Move m) {
    int f = from_sq(m), t = to_sq(m);

    char buf[6];
    buf[0] = file_char(file_of(f));
    buf[1] = rank_char(rank_of(f));
    buf[2] = file_char(file_of(t));
    buf[3] = rank_char(rank_of(t));
    int len = 4;

    int pr = promo_of(m);
    if (pr) {
        char pc = 'q';
        if (pr == 1)
            pc = 'n';
        else if (pr == 2)
            pc = 'b';
        else if (pr == 3)
            pc = 'r';
        else
            pc = 'q';
        buf[4] = pc;
        len = 5;
    }

    return std::string(buf, buf + len);
}

int hashfull_permille_fallback(const TT& tt) {
    if (tt.table.empty())
        return 0;

    const size_t N = tt.table.size();
    const size_t SAMPLE = std::min<size_t>(N, 1u << 15);

    size_t filled = 0;
    for (size_t i = 0; i < SAMPLE; i++) {
        uint64_t k = std::atomic_ref<const uint64_t>(tt.table[i].key).load(std::memory_order_relaxed);
        if (k != 0)
            filled++;
    }
    return int((filled * 1000ULL) / SAMPLE);
}

} // namespace search