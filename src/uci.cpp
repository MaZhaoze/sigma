#include "uci.h"
#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace uci {

// =====================
// 一些仅在本文件内部使用的小工具函数
// 放在匿名命名空间里，表示“仅当前 cpp 可见”
// =====================
namespace {

// 去掉字符串左侧空白字符
std::string ltrim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    return s;
}

// 去掉字符串右侧空白字符
std::string rtrim(std::string s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
    return s;
}

// 去掉字符串两端空白字符
std::string trim(std::string s) {
    return rtrim(ltrim(std::move(s)));
}

// 按空白分词，把一整行命令拆成 token 数组
// 例如 "go depth 10" -> {"go", "depth", "10"}
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok)
        out.push_back(tok);
    return out;
}

// 安全地把字符串转成 int
// 如果格式非法，或者超范围，就返回默认值 def
int to_int_safe(const std::string& s, int def) {
    if (s.empty())
        return def;

    int sign = 1;
    size_t i = 0;

    if (s[0] == '-') {
        sign = -1;
        i = 1;
    } else if (s[0] == '+') {
        i = 1;
    }

    long long x = 0;
    for (; i < s.size(); i++) {
        char c = s[i];
        if (c < '0' || c > '9')
            return def; // 一旦遇到非数字，直接视为非法
        x = x * 10 + (c - '0');
        if (x > 2000000000LL)
            break; // 提前截断，避免 long long 再继续膨胀
    }

    x *= sign;

    if (x < -2000000000LL || x > 2000000000LL)
        return def;

    return (int)x;
}

// 把字符串转成小写，方便不区分大小写比较
std::string to_lower(std::string s) {
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

// 安全地把字符串解析成 bool
// 支持 true/false, 1/0, yes/no, on/off
bool to_bool_safe(const std::string& s, bool def) {
    std::string x = to_lower(s);

    if (x == "true" || x == "1" || x == "yes" || x == "on")
        return true;
    if (x == "false" || x == "0" || x == "no" || x == "off")
        return false;

    return def;
}

// 把整数限制在 [lo, hi] 范围内
int clampi(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// =====================
// UCI 协议输出相关
// =====================

// 响应 "uci" 命令
// 输出引擎信息和所有 UCI 选项
void uci_id(const Engine&) {
    std::cout << "id name sigma-chess\n";
    std::cout << "id author Magnus\n";

    // 声明本引擎支持的 UCI 选项
    std::cout << "option name Threads type spin default 1 min 1 max 256\n";
    std::cout << "option name Hash type spin default 64 min 1 max 4096\n";
    std::cout << "option name MultiPV type spin default 1 min 1 max 1\n";
    std::cout << "option name Ponder type check default false\n";
    std::cout << "option name Move Overhead type spin default 30 min 0 max 5000\n";
    std::cout << "option name SyzygyPath type string default <empty>\n";
    std::cout << "option name Skill Level type spin default 20 min 0 max 20\n";
    std::cout << "option name SearchStats type check default false\n";

    std::cout << "uciok\n";
    std::cout.flush();
}

// 响应 "isready" 命令
void cmd_isready() {
    std::cout << "readyok\n";
    std::cout.flush();
}

// =====================
// UCI 各命令处理函数
// =====================

// 响应 "ucinewgame"
// 表示开始新对局，一般要清空历史状态、TT 等
void cmd_ucinewgame(Engine& engine) {
    engine.new_game();
}

// 响应 "setoption ..."
// 解析 name / value，并把设置同步给 Engine
void cmd_setoption(const std::vector<std::string>& tokens, Engine& engine) {
    std::string name;
    std::string value;

    // 提取 option 名字
    // 例如：setoption name Move Overhead value 50
    // name 会被拼成 "Move Overhead"
    auto itName = std::find(tokens.begin(), tokens.end(), "name");
    if (itName != tokens.end() && (itName + 1) != tokens.end()) {
        auto it = itName + 1;
        while (it != tokens.end() && *it != "value") {
            if (!name.empty())
                name += " ";
            name += *it;
            ++it;
        }
    }

    // 提取 option 的 value
    auto itVal = std::find(tokens.begin(), tokens.end(), "value");
    if (itVal != tokens.end() && (itVal + 1) != tokens.end()) {
        for (auto it = itVal + 1; it != tokens.end(); ++it) {
            if (!value.empty())
                value += " ";
            value += *it;
        }
    }

    // 转小写后统一比较，避免大小写问题
    const std::string lname = to_lower(name);

    if (lname == "hash") {
        int mb = to_int_safe(value, 64);
        mb = clampi(mb, 1, 4096);
        engine.set_hash(mb);
        return;
    }

    if (lname == "threads") {
        int n = to_int_safe(value, 1);
        n = clampi(n, 1, 256);
        engine.set_threads(n);
        return;
    }

    if (lname == "multipv") {
        int n = to_int_safe(value, 1);
        n = clampi(n, 1, 1);
        engine.set_multipv(n);
        return;
    }

    if (lname == "ponder") {
        bool b = to_bool_safe(value, false);
        engine.set_ponder(b);
        return;
    }

    if (lname == "move overhead") {
        int ms = to_int_safe(value, 30);
        ms = clampi(ms, 0, 5000);
        engine.set_move_overhead(ms);
        return;
    }

    if (lname == "syzygypath") {
        engine.set_syzygy_path(value);
        return;
    }

    if (lname == "skill level") {
        int lv = to_int_safe(value, 20);
        lv = clampi(lv, 0, 20);
        engine.set_skill_level(lv);
        return;
    }

    if (lname == "searchstats") {
        bool b = to_bool_safe(value, false);
        engine.set_search_stats(b);
        return;
    }

    // 如果是不认识的 option，这里选择静默忽略
    // 这样兼容性会更稳一点
}

// 响应 "position ..."
// 支持两种形式：
// 1. position startpos
// 2. position fen <fen字符串>
// 以及后续可选 moves 列表
void cmd_position(const std::vector<std::string>& tokens, Engine& engine) {
    if (tokens.size() < 2)
        return;

    int idx = 1;

    // position startpos
    if (tokens[idx] == "startpos") {
        engine.set_startpos();
        idx++;
    }
    // position fen <fen...>
    else if (tokens[idx] == "fen") {
        idx++;

        std::string fen;
        while (idx < (int)tokens.size() && tokens[idx] != "moves") {
            if (!fen.empty())
                fen += " ";
            fen += tokens[idx];
            idx++;
        }

        engine.set_fen(fen);
    }
    // 其他非法格式直接忽略
    else {
        return;
    }

    // 如果后面跟了 moves，就逐个走上去
    if (idx < (int)tokens.size() && tokens[idx] == "moves") {
        idx++;
        for (; idx < (int)tokens.size(); idx++) {
            engine.push_uci_move(tokens[idx]);
        }
    }
}

// 响应 "go ..."
// 这是 UCI 最核心的命令之一，用来发起搜索
void cmd_go(const std::vector<std::string>& tokens, Engine& engine) {
    bool provided_depth = false;
    bool provided_movetime = false;
    bool infinite = false;

    bool provided_wtime = false, provided_btime = false;
    bool provided_winc = false, provided_binc = false;
    bool provided_mtg = false;

    bool ponder = false;

    int depth = 0;
    int movetime = 0;

    int wtime = -1, btime = -1;
    int winc = -1, binc = -1;
    int movestogo = 0;

    // 逐个扫描 go 后面的参数
    for (int i = 1; i < (int)tokens.size(); i++) {
        const std::string& t = tokens[i];

        if (t == "ponder") {
            ponder = true;
        } else if (t == "infinite") {
            infinite = true;
        } else if (t == "depth" && i + 1 < (int)tokens.size()) {
            depth = to_int_safe(tokens[++i], 0);
            provided_depth = true;
        } else if (t == "movetime" && i + 1 < (int)tokens.size()) {
            movetime = to_int_safe(tokens[++i], 0);
            provided_movetime = true;
        } else if (t == "wtime" && i + 1 < (int)tokens.size()) {
            wtime = to_int_safe(tokens[++i], -1);
            provided_wtime = true;
        } else if (t == "btime" && i + 1 < (int)tokens.size()) {
            btime = to_int_safe(tokens[++i], -1);
            provided_btime = true;
        } else if (t == "winc" && i + 1 < (int)tokens.size()) {
            winc = to_int_safe(tokens[++i], -1);
            provided_winc = true;
        } else if (t == "binc" && i + 1 < (int)tokens.size()) {
            binc = to_int_safe(tokens[++i], -1);
            provided_binc = true;
        } else if (t == "movestogo" && i + 1 < (int)tokens.size()) {
            movestogo = to_int_safe(tokens[++i], 0);
            provided_mtg = true;
        }
    }

    // 只要给了任意时钟相关参数，就认为这是带时限搜索
    const bool hasClock =
        provided_wtime || provided_btime || provided_winc || provided_binc || provided_mtg;

    // 如果 GUI 什么限制都没给，就兜底给一个 1000ms 的 movetime
    // 否则有些引擎可能会一直搜下去
    if (!provided_depth && !provided_movetime && !infinite && !hasClock && !ponder) {
        movetime = 1000;
        provided_movetime = true;
    }

    // 把“是否提供过”转成最终传给 engine 的参数
    int depth_arg = provided_depth ? depth : 0;
    int movetime_arg = provided_movetime ? movetime : 0;

    int wtime_arg = provided_wtime ? wtime : -1;
    int btime_arg = provided_btime ? btime : -1;
    int winc_arg = provided_winc ? winc : -1;
    int binc_arg = provided_binc ? binc : -1;
    int mtg_arg = provided_mtg ? movestogo : 0;

    // 真正调用引擎开始搜索
    int bestMove = engine.go(
        depth_arg,
        movetime_arg,
        infinite,
        wtime_arg,
        btime_arg,
        winc_arg,
        binc_arg,
        mtg_arg,
        ponder
    );

    // 如果是 ponder 搜索，不应该立刻输出 bestmove
    // 通常要等到 GUI 发 ponderhit
    if (ponder) {
        return;
    }

    // 取出引擎记录的 ponder move（如果有）
    int ponderMove = engine.get_last_ponder_move();

    std::cout << "bestmove " << engine.move_to_uci(bestMove);
    if (ponderMove) {
        std::cout << " ponder " << engine.move_to_uci(ponderMove);
    }
    std::cout << "\n";
    std::cout.flush();
}

} // namespace

// =====================
// 对外唯一暴露的接口：UCI 主循环
// =====================
void loop(Engine& engine) {
    // 加速 cin/cout
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string line;

    // 不断读取 GUI 发来的每一行命令
    while (std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty())
            continue;

        auto tokens = split(line);
        const std::string& cmd = tokens[0];

        if (cmd == "uci") {
            uci_id(engine);
        } else if (cmd == "isready") {
            cmd_isready();
        } else if (cmd == "ucinewgame") {
            cmd_ucinewgame(engine);
        } else if (cmd == "setoption") {
            cmd_setoption(tokens, engine);
        } else if (cmd == "position") {
            cmd_position(tokens, engine);
        } else if (cmd == "go") {
            cmd_go(tokens, engine);
        } else if (cmd == "ponderhit") {
            // GUI 告诉你：刚才预测的那步真的下出来了
            engine.ponderhit();
        } else if (cmd == "stop") {
            // 请求停止当前搜索
            engine.stop();
        } else if (cmd == "quit") {
            // 退出前先停搜索，避免线程还在跑
            engine.stop();
            break;
        } else if (cmd == "ping") {
            // 这个不是标准 UCI，算你自己加的小调试命令
            std::cout << "pong\n";
            std::cout.flush();
        }
        // 未知命令直接忽略
    }
}

} // namespace uci