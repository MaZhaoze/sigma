#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "MoveGeneration.h"
#include "Position.h"
#include "Search.h"
#include "types.h"

class Engine {
public:
    Engine();
    ~Engine();

    // 新对局：停止搜索、重置局面、清空 TT 等
    void new_game();

    // ===== UCI 选项 =====
    void set_hash(int mb);
    void set_threads(int n);
    void set_multipv(int n);
    void set_ponder(bool b);
    void set_move_overhead(int ms);
    int move_overhead_ms() const;
    void set_syzygy_path(const std::string& s);
    void set_skill_level(int lv);
    int skill_level() const;
    void set_search_stats(bool on);

    // ===== 局面管理 =====
    void set_startpos();
    void set_fen(const std::string& fen);
    Color side_to_move() const;

    // ===== 搜索控制 =====
    void stop();
    void ponderhit();

    // 应用一个 UCI 格式走法（如 e2e4, e7e8q）
    void push_uci_move(const std::string& uciMove);

    // 最近一次搜索得到的 ponder move
    int get_last_ponder_move() const;

    // 启动一次 UCI 风格搜索
    int go(int depth, int movetime, bool infinite,
           int wtime, int btime, int winc, int binc,
           int movestogo, bool ponder);

    // 走法转 UCI 字符串
    std::string move_to_uci(int m) const;
    std::string move_to_uci(Move m) const;

private:
    // 时钟模式下分配思考时间（单位 ms）
    static int compute_think_ms(int mytime_ms, int myinc_ms, int movestogo, int move_overhead_ms);

    // 后台启动 ponder / infinite 类搜索
    void start_background_search(const search::Limits& lim);

private:
    Position pos;

    // UCI 选项缓存
    int threads_ = 1;
    int multipv_ = 1;
    bool ponder_ = false;
    int move_overhead_ms_ = 10;
    std::string syzygy_path_;
    int skill_level_ = 20;

    // 搜索 / ponder 状态
    std::atomic<bool> searching_{false};
    std::atomic<bool> pondering_{false};
    std::atomic<int> last_best_move_{0};
    std::atomic<int> last_ponder_move_{0};

    std::thread bg_thread_;
    std::mutex bg_mtx_;
};