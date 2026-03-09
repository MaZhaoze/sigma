#include "SearchCore.h"

namespace search {

int threads() {
    return g_threads.load(std::memory_order_relaxed);
}

void ensure_pool() {
    if (!g_shared_tt) {
        g_shared_tt = std::make_unique<SharedTT>();
        g_shared_tt->resize_mb(std::max(1, g_hash_mb));
    }

    if (g_pool.empty()) {
        g_pool_owner.clear();
        g_pool.clear();

        g_pool_owner.emplace_back(std::make_unique<Searcher>());
        g_pool_owner.back()->bind(g_shared_tt.get());
        g_pool.push_back(g_pool_owner.back().get());

        g_threads.store(1, std::memory_order_relaxed);
    }
}

void set_threads(int n) {
    ensure_pool();

    n = std::max(1, std::min(256, n));
    g_threads.store(n, std::memory_order_relaxed);

    g_pool_owner.clear();
    g_pool_owner.reserve(n);
    g_pool.clear();
    g_pool.reserve(n);

    for (int i = 0; i < n; i++) {
        g_pool_owner.emplace_back(std::make_unique<Searcher>());
        g_pool_owner.back()->bind(g_shared_tt.get());
        g_pool.push_back(g_pool_owner.back().get());
    }
}

Result think(Position& pos, const Limits& lim) {
    ensure_pool();

    g_nodes_total.store(0, std::memory_order_relaxed);
    g_nodes_limit.store(lim.nodes, std::memory_order_relaxed);
    start_timer(lim.movetime_ms, lim.infinite);

    if (collect_stats())
        g_ttperf.clear();

    int n = threads();
    if (n <= 1) {
        Result r = g_pool[0]->think(pos, lim, true);
        r.nodes = g_nodes_total.load(std::memory_order_relaxed);
        return r;
    }

    std::vector<std::thread> workers;
    workers.reserve(n - 1);
    std::vector<Result> threadRes((size_t)n);

    for (int i = 1; i < n; i++) {
        Position pcopy = pos;
        workers.emplace_back([i, pcopy, lim, &threadRes]() mutable {
            threadRes[(size_t)i] = g_pool[i]->think(pcopy, lim, false);
        });
    }

    Result mainRes = g_pool[0]->think(pos, lim, true);
    threadRes[0] = mainRes;

    stop();
    for (auto& th : workers)
        th.join();

    mainRes.nodes = g_nodes_total.load(std::memory_order_relaxed);
    return mainRes;
}

void set_hash_mb(int mb) {
    ensure_pool();
    g_hash_mb = std::max(1, mb);
    g_shared_tt->resize_mb(g_hash_mb);
}

void clear_tt() {
    ensure_pool();
    g_shared_tt->clear();
}

} // namespace search