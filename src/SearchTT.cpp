#include "SearchCore.h"

namespace search {

SharedTT::SharedTT() : locks(LOCKS) {}

int SharedTT::lock_index(uint64_t key) const {
    return int((key ^ (key >> 32)) & (LOCKS - 1));
}

uint64_t SharedTT::pack_payload(Move best, int16_t score, int8_t depth, uint8_t flag) {
    return (uint64_t)best
         | ((uint64_t)(uint16_t)score << 32)
         | ((uint64_t)(uint8_t)depth << 48)
         | ((uint64_t)flag << 56);
}

void SharedTT::unpack_payload(uint64_t p, Move& best, int16_t& score, int8_t& depth, uint8_t& flag) {
    best  = (Move)(p & 0xffffffffu);
    score = (int16_t)((p >> 32) & 0xffffu);
    depth = (int8_t)((p >> 48) & 0xffu);
    flag  = (uint8_t)((p >> 56) & 0xffu);
}

bool SharedTT::probe_copy(uint64_t key, TTEntry& out) {
    const bool cs = collect_stats();
    uint64_t t0 = 0;
    if (cs) t0 = now_ns();

    if (cs) {
        g_ttperf.probeCalls.fetch_add(1, std::memory_order_relaxed);
        g_ttperf.probeLockWaitNs.fetch_add(now_ns() - t0, std::memory_order_relaxed);
    }

    TTEntry* e = tt.probe(key);
    if (!e) {
        if (cs) g_ttperf.probeMiss.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    auto keyRef = std::atomic_ref<uint64_t>(e->key);
    uint64_t k1 = keyRef.load(std::memory_order_acquire);
    if (k1 != key) {
        if (cs) g_ttperf.probeMiss.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    auto payloadRef = std::atomic_ref<uint64_t>(e->payload);
    uint64_t payload = payloadRef.load(std::memory_order_relaxed);

    uint64_t k2 = keyRef.load(std::memory_order_acquire);
    if (k2 != k1 || k2 != key) {
        if (cs) g_ttperf.probeMiss.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    out.key = key;
    unpack_payload(payload, out.best, out.score, out.depth, out.flag);

    if (cs) g_ttperf.probeHits.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void SharedTT::store(uint64_t key, Move best, int16_t score, int16_t depth, uint8_t flag) {
    const bool cs = collect_stats();
    uint64_t t0 = 0;
    if (cs) t0 = now_ns();

    if (cs) {
        g_ttperf.storeCalls.fetch_add(1, std::memory_order_relaxed);
        g_ttperf.storeLockWaitNs.fetch_add(now_ns() - t0, std::memory_order_relaxed);
    }

    TTEntry* e = tt.probe(key);
    if (!e) return;

    auto keyRef = std::atomic_ref<uint64_t>(e->key);
    auto payloadRef = std::atomic_ref<uint64_t>(e->payload);

    uint64_t oldKey = keyRef.load(std::memory_order_acquire);
    uint64_t oldPayload = payloadRef.load(std::memory_order_relaxed);

    Move oldBest = 0;
    int16_t oldScore = 0;
    int8_t oldDepth = -128;
    uint8_t oldFlag = TT_ALPHA;
    unpack_payload(oldPayload, oldBest, oldScore, oldDepth, oldFlag);

    int clipped = (depth > 127 ? 127 : (depth < -128 ? -128 : depth));
    int8_t newDepth = static_cast<int8_t>(clipped);

    if (oldKey != key || depth >= oldDepth) {
        payloadRef.store(pack_payload(best, score, newDepth, flag), std::memory_order_relaxed);
        keyRef.store(key, std::memory_order_release);
        if (cs) g_ttperf.storeWrites.fetch_add(1, std::memory_order_relaxed);
    } else if (cs) {
        g_ttperf.storeSkips.fetch_add(1, std::memory_order_relaxed);
    }
}

void SharedTT::resize_mb(int mb) {
    std::unique_lock<std::shared_mutex> tableGuard(table_mtx);
    tt.resize_mb(mb);
}

void SharedTT::clear() {
    std::unique_lock<std::shared_mutex> tableGuard(table_mtx);
    for (auto& e : tt.table) e = TTEntry{};
}

int SharedTT::hashfull_permille() const {
    const bool cs = collect_stats();
    uint64_t t0 = 0;
    if (cs) t0 = now_ns();

    std::unique_lock<std::shared_mutex> tableGuard(table_mtx);

    if (cs) {
        g_ttperf.hashfullCalls.fetch_add(1, std::memory_order_relaxed);
        g_ttperf.hashfullWaitNs.fetch_add(now_ns() - t0, std::memory_order_relaxed);
    }

    return hashfull_permille_fallback(tt);
}

} // namespace search