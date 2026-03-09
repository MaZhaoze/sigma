#include "Engine.h"

#include <algorithm>
#include <vector>

// =====================
// 构造 / 析构
// =====================

// 构造时默认把局面设成初始局面
Engine::Engine() {
    pos.set_startpos();
}

// 析构时一定先停掉后台搜索线程
// 否则对象都要销毁了，线程还在跑，会非常危险
Engine::~Engine() {
    stop();
}

// =====================
// 新对局 / UCI 选项
// =====================

// 开新对局：
// 1. 先停掉可能存在的搜索
// 2. 把局面恢复到初始局面
// 3. 清空置换表
// 4. 清掉上次 ponder move 记录
void Engine::new_game() {
    stop();
    pos.set_startpos();
    search::clear_tt();
    last_ponder_move_.store(0, std::memory_order_relaxed);
}

// 设置 Hash 大小
// 一般改 hash 会影响搜索模块内部状态，所以先 stop 再改
void Engine::set_hash(int mb) {
    stop();
    search::set_hash_mb(mb);
}

// 设置线程数
// 同样为了安全，先停搜索再修改
void Engine::set_threads(int n) {
    stop();
    threads_ = std::max(1, n);
    search::set_threads(threads_);
}

// 目前只是缓存下来，实际搜索仍然只输出单 PV
void Engine::set_multipv(int n) {
    (void)n;
    multipv_ = 1;
}

// 是否允许 ponder
void Engine::set_ponder(bool b) {
    ponder_ = b;
}

// 设置时间管理中的额外开销（比如 GUI 通信、系统调度损耗）
// 不允许为负数
void Engine::set_move_overhead(int ms) {
    move_overhead_ms_ = std::max(0, ms);
}

int Engine::move_overhead_ms() const {
    return move_overhead_ms_;
}

// 设置 Syzygy 路径
// 这里目前只是缓存字符串，真正怎么使用取决于你的 search / tb 模块
void Engine::set_syzygy_path(const std::string& s) {
    syzygy_path_ = s;
}

// 设置 skill level，限制在 [0, 20]
void Engine::set_skill_level(int lv) {
    skill_level_ = std::max(0, std::min(20, lv));
}

int Engine::skill_level() const {
    return skill_level_;
}

// 是否统计搜索数据
void Engine::set_search_stats(bool on) {
    search::set_collect_stats(on);
}

// =====================
// 局面管理
// =====================

// 设成初始局面
void Engine::set_startpos() {
    pos.set_startpos();
}

// 设成给定 FEN
void Engine::set_fen(const std::string& fen) {
    pos.set_fen(fen);
}

// 当前轮到哪一方走
Color Engine::side_to_move() const {
    return pos.side;
}

// =====================
// 搜索控制
// =====================

// 停止任意正在进行的搜索，并等待后台线程结束
void Engine::stop() {
    // 先通知搜索模块“该停了”
    search::stop();

    // 然后安全地 join 后台线程
    std::lock_guard<std::mutex> g(bg_mtx_);
    if (bg_thread_.joinable())
        bg_thread_.join();

    // 状态复位
    searching_.store(false, std::memory_order_release);
    pondering_.store(false, std::memory_order_release);
}

// UCI 的 ponderhit：
// 表示 GUI 告诉引擎，“你刚才猜测的那步真的下出来了”
// 你这份代码当前的处理方式是：直接停掉后台 ponder
void Engine::ponderhit() {
    if (!pondering_.load(std::memory_order_acquire))
        return;
    stop();
}

// 把一个 UCI 字符串走法应用到当前局面上
// 做法很朴素：
// 1. 先生成所有合法走法
// 2. 把每个走法转成 UCI 字符串
// 3. 找到匹配项后执行
void Engine::push_uci_move(const std::string& uciMove) {
    std::vector<Move> moves;
    moves.reserve(256);

    movegen::generate_legal(pos, moves);

    for (Move m : moves) {
        if (move_to_uci(m) == uciMove) {
            Undo u = pos.do_move(m);
            (void)u; // 如果你当前不需要这个 Undo，就显式丢弃
            return;
        }
    }
}

// 返回最近一次搜索记录下来的 ponder move
int Engine::get_last_ponder_move() const {
    return last_ponder_move_.load(std::memory_order_relaxed);
}

// =====================
// 核心：启动搜索
// =====================

// 这是 UCI 的 go 命令对应入口
// 参数优先级：ponder > infinite > movetime > clock > depth
int Engine::go(int depth, int movetime, bool infinite,
               int wtime, int btime, int winc, int binc,
               int movestogo, bool ponder) {
    // 为避免重入，开新搜索前先停掉旧搜索
    stop();

    search::Limits lim{};
    lim.depth = 0;
    lim.movetime_ms = 0;
    lim.infinite = false;

    const bool depth_given = (depth > 0);
    const bool movetime_given = (movetime > 0);

    const bool hasClock =
        (wtime > 0) || (btime > 0) || (winc > 0) || (binc > 0) || (movestogo > 0);

    // ===== 1. ponder 模式 =====
    // ponder 本质上是后台无限搜索，不应该立刻输出 bestmove
    if (ponder) {
        lim.infinite = true;
        lim.movetime_ms = 0;
        lim.depth = depth_given ? depth : 0;

        start_background_search(lim);
        pondering_.store(true, std::memory_order_release);

        return 0; // UCI 协议下，ponder 开始时不输出 bestmove
    }

    // ===== 2. infinite 模式 =====
    // 不受时间限制，一直搜到 stop
    if (infinite) {
        lim.infinite = true;
        lim.movetime_ms = 0;
        lim.depth = depth_given ? depth : 0;

        search::Result r = search::think(pos, lim);
        last_ponder_move_.store((int)r.ponderMove, std::memory_order_relaxed);
        return (int)r.bestMove;
    }

    // ===== 3. movetime 模式 =====
    // 如果明确给了 movetime，则它优先级高于 clock
    if (movetime_given) {
        lim.infinite = false;
        lim.movetime_ms = std::max(1, movetime);
        lim.depth = depth_given ? depth : 0;

        search::Result r = search::think(pos, lim);
        last_ponder_move_.store((int)r.ponderMove, std::memory_order_relaxed);
        return (int)r.bestMove;
    }

    // ===== 4. clock 模式 =====
    // 根据 side to move 选取我方剩余时间 / increment
    if (hasClock) {
        int myTime = (pos.side == WHITE ? wtime : btime);
        int myInc  = (pos.side == WHITE ? winc  : binc);

        if (myTime < 0) myTime = 0;
        if (myInc  < 0) myInc  = 0;

        int t_ms = compute_think_ms(myTime, myInc, movestogo, move_overhead_ms_);
        if (t_ms < 1)
            t_ms = 1;

        lim.movetime_ms = t_ms;
        lim.infinite = false;
    } else {
        // ===== 5. 仅 depth 模式 =====
        // 没给时钟、没给 movetime，那就是普通 depth-only 搜索
        lim.movetime_ms = 0;
        lim.infinite = false;
    }

    // 如果给了 depth，就按给定 depth；
    // 否则 depth=0 通常表示“由搜索器自行决定”
    lim.depth = depth_given ? depth : 0;

    // Skill Level 只在“时钟模式 + 没有强制 depth”的情况下生效
    // 通过限制深度和压缩用时来模拟弱一点的水平
    if (!depth_given && hasClock && skill_level_ < 20) {
        // 0..19 -> 4..13
        int capDepth = 4 + skill_level_ / 2;
        capDepth = std::max(1, std::min(64, capDepth));
        lim.depth = capDepth;

        // 0..19 -> 大约 40%..90% 的时间
        int factor = 40 + (skill_level_ * 50) / 19;
        lim.movetime_ms = std::max(1, (lim.movetime_ms * factor) / 100);
    }

    // 最终同步执行一次搜索
    search::Result r = search::think(pos, lim);
    last_ponder_move_.store((int)r.ponderMove, std::memory_order_relaxed);
    return (int)r.bestMove;
}

// =====================
// 走法转字符串
// =====================

// int 版本只是转调 Move 版本
std::string Engine::move_to_uci(int m) const {
    return move_to_uci((Move)m);
}

// 把内部编码走法转成 UCI 格式，如 e2e4 / e7e8q
std::string Engine::move_to_uci(Move m) const {
    int from = from_sq(m);
    int to = to_sq(m);

    std::string s;
    s += char('a' + file_of(from));
    s += char('1' + rank_of(from));
    s += char('a' + file_of(to));
    s += char('1' + rank_of(to));

    int promo = promo_of(m);
    if (promo) {
        char pc = 'q';
        if (promo == 1)
            pc = 'n';
        else if (promo == 2)
            pc = 'b';
        else if (promo == 3)
            pc = 'r';
        else if (promo == 4)
            pc = 'q';
        s += pc;
    }

    return s;
}

// =====================
// 私有辅助函数
// =====================

// 在时钟模式下估算本步应该花多少 ms
// 这只是一个启发式时间管理，不是唯一正确答案
int Engine::compute_think_ms(int mytime_ms, int myinc_ms, int movestogo, int move_overhead_ms) {
    if (mytime_ms <= 0)
        return 1;

    // 先扣掉预留开销
    int tleft = mytime_ms - std::max(0, move_overhead_ms);
    if (tleft < 1)
        tleft = 1;

    // 极快棋 / 几乎没时间时，直接保守一点
    if (tleft <= 200) {
        return std::max(1, tleft / 4);
    }

    int t = 0;

    if (movestogo > 0) {
        // 固定步数控制
        // 例如“40 步包干”
        int mtg = std::max(1, movestogo);

        // 剩余总视野 = 当前剩余时间 + 后续可能拿到的 increment
        int horizon = tleft + (myinc_ms * std::max(0, mtg - 1));
        int opt = horizon / mtg;

        t = opt;

        // 最多别一下子花掉剩余时间的 70%
        t = std::min(t, (tleft * 70) / 100);
    } else {
        // 猝死时限（sudden death）
        // 这里用较平滑的策略，避免每步都只分到很碎的时间
        int div = 16;
        if (tleft >= 12000)
            div = 12;
        else if (tleft >= 6000)
            div = 14;

        int horizon = tleft + myinc_ms * 18;
        int opt = horizon / div;

        t = opt + (myinc_ms / 2);

        int capPct = (tleft >= 8000) ? 72 : 62;
        t = std::min(t, (tleft * capPct) / 100);
    }

    // 给一个基础下限，避免太短
    t = std::max(t, 5);

    // 略微减一点，给 stop / GUI / 系统调度留余地
    if (t > 2)
        t -= 2;

    // 动态最小值：避免某些情况下思考时间过小
    int dynMin = 5;
    if (movestogo > 0) {
        int mtg = std::max(1, movestogo);
        dynMin = std::max(dynMin, tleft / std::max(7, mtg * 2));
    } else {
        dynMin = std::max(dynMin, tleft / 30);
    }

    if (myinc_ms > 0)
        dynMin = std::max(dynMin, myinc_ms / 2);

    dynMin = std::min(dynMin, std::max(5, tleft / 3));

    t = std::max(t, dynMin);
    t = std::min(t, std::max(5, tleft / 2));

    return t;
}

// 启动后台搜索线程
// 主要给 ponder 用
void Engine::start_background_search(const search::Limits& lim) {
    // 为了避免后台线程直接改动 engine 当前局面，
    // 这里复制一份当前局面给线程自己搜
    Position pcopy = pos;

    std::lock_guard<std::mutex> g(bg_mtx_);

    // 如果旧线程还存在，先回收
    if (bg_thread_.joinable())
        bg_thread_.join();

    searching_.store(true, std::memory_order_release);

    bg_thread_ = std::thread([this, pcopy, lim]() mutable {
        // 后台搜索通常不应乱输出 info，避免和前台输出串行冲突
        search::Result r = search::think(pcopy, lim);

        last_best_move_.store((int)r.bestMove, std::memory_order_relaxed);
        last_ponder_move_.store((int)r.ponderMove, std::memory_order_relaxed);
        searching_.store(false, std::memory_order_release);
    });
}