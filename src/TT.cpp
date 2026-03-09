#include "TT.h"

#include <algorithm>

namespace search {

// =====================
// TTEntry 默认构造
// key=0 表示空槽
// depth=-1 表示无有效深度
// =====================
TTEntry::TTEntry()
    : key(0), best(0), score(0), depth(-1), flag(TT_EXACT) {}

// =====================
// 按 MB 大小重建置换表
//
// 设计思路：
// 1. 至少分配 1MB
// 2. 按 TTEntry 大小算出大致能放多少条
// 3. 向上取到 2 的幂，方便用 mask 快速取模
// 4. 全部初始化为空条目
// =====================
void TT::resize_mb(int mb) {
    // 最少按 1MB 处理，避免传进来 0 或负数
    size_t bytes = size_t(std::max(1, mb)) * 1024ULL * 1024ULL;

    // 估算需要多少个条目
    size_t n = std::max<size_t>(1, bytes / sizeof(TTEntry));

    // 向上取整到 2 的幂
    // 这样 probe 时可以直接 index = key & mask
    size_t p2 = 1;
    while (p2 < n)
        p2 <<= 1;

    table.assign(p2, TTEntry{});
    mask = p2 - 1;
}

// =====================
// probe：根据 zobrist key 查对应槽位
//
// 当前实现是“单槽单替换”模型：
// 每个 key 只映射到一个固定位置，没有 bucket / cluster。
// 优点是简单、快；缺点是冲突时替换比较粗暴。
//
// 如果表还没分配，返回 dummy，避免空指针。
// =====================
TTEntry* TT::probe(uint64_t key_) {
    if (table.empty())
        return &dummy;

    // 因为 table.size() 是 2 的幂，
    // 所以 key & mask 等价于 key % size，但更快
    return &table[size_t(key_) & mask];
}

// =====================
// 清空整张置换表
// 直接把所有条目重置成默认空条目
// =====================
void TT::clear() {
    if (!table.empty()) {
        std::fill(table.begin(), table.end(), TTEntry{});
    }
}

} // namespace search