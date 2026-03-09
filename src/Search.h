#pragma once
#include <cstdint>
#include <string>

#include "types.h"
#include "Position.h"

namespace search {

struct SearchInfo {
    int depth = 0;
    int seldepth = 0;
    uint64_t nodes = 0;
    int time_ms = 0;
    int nps = 0;
    int hashfull = 0;
};

static constexpr int INF = 30000;
static constexpr int MATE = 29000;

struct Limits {
    int depth = 7;
    int movetime_ms = 0;
    uint64_t nodes = 0;
    bool infinite = false;
};

struct Result {
    Move bestMove = 0;
    Move ponderMove = 0;
    int score = 0;
    uint64_t nodes = 0;
};

// 对外公开 API
void stop();
Result think(Position& pos, const Limits& lim);

void set_threads(int n);
void set_hash_mb(int mb);
void clear_tt();

void set_collect_stats(bool on);
bool collect_stats();

// 常用工具函数
std::string move_to_uci(Move m);

} // namespace search