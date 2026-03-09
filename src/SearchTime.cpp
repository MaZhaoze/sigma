#include "SearchCore.h"

namespace search {

void Searcher::batch_time_check_soft() {
    int64_t end = g_endTimeMs.load(std::memory_order_relaxed);
    if (end == 0) return;
    if (now_ms() >= end) {
        tmLastCause = TM_CAUSE_HARD;
        if (canSignalGlobalStop)
            g_stop.store(true, std::memory_order_relaxed);
    }
}

void Searcher::flush_nodes_batch() {
    if (nodes_batch) {
        uint64_t t0 = 0;
        const bool cs = collect_stats();
        if (cs) t0 = now_ns();

        g_nodes_total.fetch_add(nodes_batch, std::memory_order_relaxed);

        if (cs) {
            ss.nodeBatchFlush++;
            ss.nodeBatchFlushNs += (now_ns() - t0);
        }
        nodes_batch = 0;
    }
}

bool Searcher::stop_or_time_up(bool rootNode) {
    if (g_stop.load(std::memory_order_relaxed)) {
        if (tmLastCause == TM_CAUSE_NONE)
            tmLastCause = TM_CAUSE_OTHER;
        return true;
    }

    int64_t end = g_endTimeMs.load(std::memory_order_relaxed);
    if (end == 0) return false;

    const uint32_t mask = rootNode ? TIME_CHECK_MASK_ROOT : TIME_CHECK_MASK_NODE;
    if ((time_check_tick++ & mask) != 0)
        return false;

    if (collect_stats())
        ss.timeChecks++;

    if (now_ms() >= end) {
        tmLastCause = TM_CAUSE_HARD;
        if (canSignalGlobalStop)
            g_stop.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void Searcher::add_node() {
    nodes++;
    nodes_batch++;

    if ((nodes_batch & 1023ULL) == 0ULL) {
        uint64_t nodeCap = g_nodes_limit.load(std::memory_order_relaxed);
        if (nodeCap) {
            if (collect_stats())
                ss.nodeBatchCapCheck++;
            uint64_t totalApprox = g_nodes_total.load(std::memory_order_relaxed) + nodes_batch;
            if (totalApprox >= nodeCap) {
                g_stop.store(true, std::memory_order_relaxed);
                return;
            }
        }
    }

    if (nodes_batch == NODE_BATCH) {
        uint64_t t0 = 0;
        const bool cs = collect_stats();
        if (cs) t0 = now_ns();

        uint64_t before = g_nodes_total.fetch_add(NODE_BATCH, std::memory_order_relaxed);

        if (cs) {
            ss.nodeBatchFlush++;
            ss.nodeBatchFlushNs += (now_ns() - t0);
        }

        nodes_batch = 0;

        uint64_t nodeCap = g_nodes_limit.load(std::memory_order_relaxed);
        if (nodeCap && (before + NODE_BATCH) >= nodeCap) {
            g_stop.store(true, std::memory_order_relaxed);
            return;
        }

        batch_time_check_soft();
    }
}

} // namespace search