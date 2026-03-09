#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "types.h"
#include "Position.h"

namespace eval {

// Static evaluation with MG/EG interpolation and lightweight structure terms.

// =====================================================
// Score (MG/EG) and helpers
// =====================================================
struct Score {
    int mg = 0;
    int eg = 0;
    Score() = default;
    Score(int m, int e) : mg(m), eg(e) {}
};

inline Score operator+(Score a, Score b) {
    return {a.mg + b.mg, a.eg + b.eg};
}
inline Score operator-(Score a, Score b) {
    return {a.mg - b.mg, a.eg - b.eg};
}
inline Score& operator+=(Score& a, Score b) {
    a.mg += b.mg;
    a.eg += b.eg;
    return a;
}
inline Score& operator-=(Score& a, Score b) {
    a.mg -= b.mg;
    a.eg -= b.eg;
    return a;
}

inline int clampi(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}
inline int manhattan(int a, int b) {
    return std::abs(file_of(a) - file_of(b)) + std::abs(rank_of(a) - rank_of(b));
}
inline int chebyshev(int a, int b) {
    return std::max(std::abs(file_of(a) - file_of(b)), std::abs(rank_of(a) - rank_of(b)));
}
inline Color flip_color(Color c) {
    return (c == WHITE) ? BLACK : WHITE;
}
inline int mirror_sq(int sq) {
    return sq ^ 56;
}

// =====================================================
// Material values (MG/EG)
// =====================================================
static constexpr int MG_VAL[7] = {0, 100, 320, 330, 500, 900, 0};
static constexpr int EG_VAL[7] = {0, 120, 300, 320, 520, 900, 0};

inline int mg_value(PieceType pt) {
    if (pt < PAWN || pt > KING)
        return 0;
    return MG_VAL[(int)pt];
}

// =====================================================
// PSQT (MG/EG)
// =====================================================
static constexpr int PST_P_MG[64] = {0,  0,  0,   0,  0,  0,   0,  0,  5,  10, 10, -20, -20, 10, 10, 5,
                                     5,  -5, -10, 0,  0,  -10, -5, 5,  0,  0,  0,  20,  20,  0,  0,  0,
                                     5,  5,  10,  25, 25, 10,  5,  5,  10, 10, 20, 30,  30,  20, 10, 10,
                                     50, 50, 50,  50, 50, 50,  50, 50, 0,  0,  0,  0,   0,   0,  0,  0};
static constexpr int PST_P_EG[64] = {0, 0, 0, 0, 0,  0,  0,  0,  10, 10, 10, 10, 10, 10, 10, 10, 8, 8, 8, 12, 12, 8,
                                     8, 8, 6, 6, 10, 14, 14, 10, 6,  6,  4,  4,  6,  10, 10, 6,  4, 4, 2, 2,  2,  6,
                                     6, 2, 2, 2, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0};

static constexpr int PST_N_MG[64] = {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   5,   5,   0,   -20, -40,
                                     -30, 5,   10,  15,  15,  10,  5,   -30, -30, 0,   15,  20,  20,  15,  0,   -30,
                                     -30, 5,   15,  20,  20,  15,  5,   -30, -30, 0,   10,  15,  15,  10,  0,   -30,
                                     -40, -20, 0,   0,   0,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50};
static constexpr int PST_N_EG[64] = {-40, -25, -20, -15, -15, -20, -25, -40, -25, -10, 0,   5,   5,   0,   -10, -25,
                                     -20, 5,   10,  15,  15,  10,  5,   -20, -15, 5,   15,  20,  20,  15,  5,   -15,
                                     -15, 5,   15,  20,  20,  15,  5,   -15, -20, 5,   10,  15,  15,  10,  5,   -20,
                                     -25, -10, 0,   5,   5,   0,   -10, -25, -40, -25, -20, -15, -15, -20, -25, -40};

static constexpr int PST_B_MG[64] = {-20, -10, -10, -10, -10, -10, -10, -20, -10, 5,   0,   0,   0,   0,   5,   -10,
                                     -10, 10,  10,  10,  10,  10,  10,  -10, -10, 0,   10,  10,  10,  10,  0,   -10,
                                     -10, 5,   5,   10,  10,  5,   5,   -10, -10, 0,   5,   10,  10,  5,   0,   -10,
                                     -10, 0,   0,   0,   0,   0,   0,   -10, -20, -10, -10, -10, -10, -10, -10, -20};
static constexpr int PST_B_EG[64] = {-10, -5, -5, -5, -5, -5, -5, -10, -5,  5,  0,  0,  0,  0,  5,  -5,
                                     -5,  8,  10, 10, 10, 10, 8,  -5,  -5,  0,  10, 12, 12, 10, 0,  -5,
                                     -5,  0,  10, 12, 12, 10, 0,  -5,  -5,  8,  10, 10, 10, 10, 8,  -5,
                                     -5,  5,  0,  0,  0,  0,  5,  -5,  -10, -5, -5, -5, -5, -5, -5, -10};

static constexpr int PST_R_MG[64] = {0, 0,  5,  10, 10, 5,  0,  0,  -5, 0,  0,  0, 0, 0, 0, -5, -5, 0,  0,  0, 0, 0,
                                     0, -5, -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0, 0, 0, 0, 0,  0,  -5, -5, 0, 0, 0,
                                     0, 0,  0,  -5, 5,  10, 10, 10, 10, 10, 10, 5, 0, 0, 0, 2,  2,  0,  0,  0};
static constexpr int PST_R_EG[64] = {0,  0, 5, 8, 8, 5,  0,  0, 0, 5, 8, 10, 10, 8,  5,  0, 0, 5, 8, 10, 10, 8,
                                     5,  0, 0, 5, 8, 10, 10, 8, 5, 0, 0, 5,  8,  10, 10, 8, 5, 0, 0, 5,  8,  10,
                                     10, 8, 5, 0, 0, 0,  5,  8, 8, 5, 0, 0,  0,  0,  0,  3, 3, 0, 0, 0};

static constexpr int PST_Q_MG[64] = {-20, -10, -10, -5, -5, -10, -10, -20, -10, 0,   0,   0,  0,  0,   0,   -10,
                                     -10, 0,   5,   5,  5,  5,   0,   -10, -5,  0,   5,   5,  5,  5,   0,   -5,
                                     0,   0,   5,   5,  5,  5,   0,   -5,  -10, 5,   5,   5,  5,  5,   0,   -10,
                                     -10, 0,   5,   0,  0,  0,   0,   -10, -20, -10, -10, -5, -5, -10, -10, -20};
static constexpr int PST_Q_EG[64] = {-10, -5, -5, -2, -2, -5, -5, -10, -5,  0,  0,  0,  0,  0,  0,  -5,
                                     -5,  0,  3,  3,  3,  3,  0,  -5,  -2,  0,  3,  4,  4,  3,  0,  -2,
                                     -2,  0,  3,  4,  4,  3,  0,  -2,  -5,  0,  3,  3,  3,  3,  0,  -5,
                                     -5,  0,  0,  0,  0,  0,  0,  -5,  -10, -5, -5, -2, -2, -5, -5, -10};

static constexpr int PST_K_MG[64] = {-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                                     -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                                     -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10,
                                     20,  20,  0,   0,   0,   0,   20,  20,  20,  30,  10,  0,   0,   10,  30,  20};
static constexpr int PST_K_EG[64] = {-50, -30, -30, -30, -30, -30, -30, -50, -30, -10, 0,   0,   0,   0,   -10, -30,
                                     -30, 0,   10,  15,  15,  10,  0,   -30, -30, 0,   15,  20,  20,  15,  0,   -30,
                                     -30, 0,   15,  20,  20,  15,  0,   -30, -30, 0,   10,  15,  15,  10,  0,   -30,
                                     -30, -10, 0,   0,   0,   0,   -10, -30, -50, -30, -30, -30, -30, -30, -30, -50};

// =====================================================
// Weights (base)
// =====================================================
static constexpr int BISHOP_PAIR_MG = 30;
static constexpr int BISHOP_PAIR_EG = 45;

static constexpr int DOUBLED_PAWN_PEN = 10;
static constexpr int ISOLATED_PAWN_PEN = 12;
static constexpr int BACKWARD_PAWN_PEN = 10;
static constexpr int CONNECTED_PAWN_BONUS = 6;
static constexpr int CHAIN_PAWN_BONUS = 5;

static constexpr int PASSED_MG[8] = {0, 2, 4, 8, 14, 22, 40, 0};
static constexpr int PASSED_EG[8] = {0, 8, 12, 18, 28, 45, 75, 0};
static constexpr int PASSED_PROTECTED_MG = 6;
static constexpr int PASSED_PROTECTED_EG = 10;
static constexpr int PASSED_BLOCKED_MG = -10;
static constexpr int PASSED_BLOCKED_EG = -16;
static constexpr int PASSED_CONNECTED_EG = 18;
static constexpr int OUTSIDE_PASSER_EG = 12;

static constexpr int SHIELD_PAWN_MG = 10;
static constexpr int SHIELD_MISSING_MG = 12;

static constexpr int MOB_N_MG = 4, MOB_N_EG = 2;
static constexpr int MOB_B_MG = 3, MOB_B_EG = 2;
static constexpr int MOB_R_MG = 2, MOB_R_EG = 2;
static constexpr int MOB_Q_MG = 1, MOB_Q_EG = 1;

static constexpr int ROOK_OPEN_FILE_MG = 22;
static constexpr int ROOK_SEMIOPEN_FILE_MG = 12;
static constexpr int ROOK_7TH_MG = 18;
static constexpr int ROOK_CONNECTED_MG = 10;

static constexpr int EARLY_QUEEN_PEN_MG = 6;

static constexpr int OUTPOST_MG = 14;
static constexpr int OUTPOST_EG = 8;
static constexpr int KNIGHT_RIM_PEN_MG = 12;
static constexpr int KNIGHT_RIM_PEN_EG = 6;
static constexpr int BAD_BISHOP_MG = 8;
static constexpr int BAD_BISHOP_EG = 4;

static constexpr int CENTER_CONTROL_MG = 2;

// Side-to-move weights for tempo and king safety tuning.
struct Weights {
    int tempoMG;
    int shieldMissingExtraMG;
    int ksAttackWeight;
    int ksAttackerWeight;
    int ksOpenFileMG;
    int ksSemiOpenMG;
    int ksScalePct;
};

// Attack generation for king safety.
struct AttackInfo {
    uint8_t attacks[2][64]{};
    uint8_t attackers[2][64]{};
};

// Pawn structure helpers.
struct PawnInfo {
    int fileCount[2][8]{};
    uint8_t ranksMask[2][8]{};
};

// 统一的评估入口：返回 cp（从当前行棋方视角）
int evaluate(const Position& pos);

// ===== 下面这些是拆到 cpp 的实现声明 =====
Weights weights_for(Color stm);

int game_phase_256(const Position& pos);

void add_attack(AttackInfo& ai, Color c, int sq);
bool on_board(int f, int r);

void gen_knight_attacks(const Position& pos, AttackInfo& ai, Color c, int from);
void gen_king_attacks(const Position& pos, AttackInfo& ai, Color c, int from);
void gen_pawn_attacks(const Position& pos, AttackInfo& ai, Color c, int from);
void gen_slider_attacks(const Position& pos, AttackInfo& ai, Color c, int from, const int* dirs, int ndirs);

AttackInfo compute_attacks(const Position& pos);

int mobility_knight(const Position& pos, Color c, int from);
int mobility_slider(const Position& pos, Color c, int from, const int* dirs, int ndirs);

PawnInfo gather_pawns(const Position& pos);
bool pawn_on(const Position& pos, int sq, Color c);
int low_bit(uint8_t m);

Score eval_pawns(const Position& pos, const PawnInfo& pi, int kingSqW, int kingSqB, const Weights& W);
Score eval_king_safety(const Position& pos, const PawnInfo& pi, const AttackInfo& ai, int kingSqW,
                       int kingSqB, int phase, const Weights& W);
Score eval_pieces(const Position& pos, const PawnInfo& pi, const AttackInfo& ai, int kingSqW, int kingSqB);

} // namespace eval