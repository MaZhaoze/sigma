#pragma once
#include <cstdint>
#include <vector>

#include "types.h" // Move encoding.

namespace search {

// 置换表条目标记
enum TTFlag : uint8_t {
    TT_EXACT = 0,
    TT_ALPHA = 1,
    TT_BETA  = 2
};

// 单条 TT 记录
// alignas(16) 让每条记录按 16 字节对齐，
// 有利于缓存行为，也方便你控制表大小
struct alignas(16) TTEntry {
    uint64_t key = 0; // 完整 zobrist key

    union {
        struct {
            Move best;      // 最优着法
            int16_t score;  // 存储分数（可能已做 TT 调整）
            int8_t depth;   // 搜索深度（ply）
            uint8_t flag;   // TTFlag
        };
        uint64_t payload = 0; // 打包视图
    };

    TTEntry();
};

// 保证 TTEntry 大小固定为 16 字节
static_assert(sizeof(TTEntry) == 16, "TTEntry size changed; review layout/alignment.");

struct TT {
    std::vector<TTEntry> table;
    uint64_t mask = 0;

    TTEntry dummy; // 当表为空时返回这个占位对象

    void resize_mb(int mb);
    TTEntry* probe(uint64_t key_);
    void clear();
};

} // namespace search