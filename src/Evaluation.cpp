#include "Evaluation.h"

namespace eval {

// Slight asymmetric tuning by side to move.
Weights weights_for(Color stm) {
    if (stm == WHITE) {
        return Weights{
            8,  // tempoMG
            0,  // shieldMissingExtraMG
            5,  // ksAttackWeight
            10, // ksAttackerWeight
            16, // ksOpenFileMG
            9,  // ksSemiOpenMG
            100 // ksScalePct
        };
    } else {
        return Weights{
            6,  // tempoMG
            2,  // shieldMissingExtraMG (black more sensitive)
            6,  // ksAttackWeight
            12, // ksAttackerWeight
            18, // ksOpenFileMG
            10, // ksSemiOpenMG
            110 // ksScalePct (overall danger ~+10%)
        };
    }
}

// Phase (0..256) for MG/EG interpolation.
int game_phase_256(const Position& pos) {
    int phase = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;
        switch (type_of(p)) {
        case KNIGHT:
            phase += 1;
            break;
        case BISHOP:
            phase += 1;
            break;
        case ROOK:
            phase += 2;
            break;
        case QUEEN:
            phase += 4;
            break;
        default:
            break;
        }
    }
    phase = clampi(phase, 0, 24);
    return (phase * 256) / 24;
}

void add_attack(AttackInfo& ai, Color c, int sq) {
    int ci = (c == WHITE) ? 0 : 1;
    if ((unsigned)sq < 64u) {
        ai.attacks[ci][sq]++;
        ai.attackers[ci][sq] = (uint8_t)std::min<int>(255, ai.attackers[ci][sq] + 1);
    }
}

bool on_board(int f, int r) {
    return (unsigned)f < 8u && (unsigned)r < 8u;
}

void gen_knight_attacks(const Position&, AttackInfo& ai, Color c, int from) {
    static const int df[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int dr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    int f = file_of(from), r = rank_of(from);
    for (int i = 0; i < 8; i++) {
        int nf = f + df[i], nr = r + dr[i];
        if (!on_board(nf, nr))
            continue;
        add_attack(ai, c, make_sq(nf, nr));
    }
}

void gen_king_attacks(const Position&, AttackInfo& ai, Color c, int from) {
    int f = file_of(from), r = rank_of(from);
    for (int df = -1; df <= 1; df++)
        for (int dr = -1; dr <= 1; dr++) {
            if (!df && !dr)
                continue;
            int nf = f + df, nr = r + dr;
            if (!on_board(nf, nr))
                continue;
            add_attack(ai, c, make_sq(nf, nr));
        }
}

void gen_pawn_attacks(const Position&, AttackInfo& ai, Color c, int from) {
    int f = file_of(from), r = rank_of(from);
    int dr = (c == WHITE) ? +1 : -1;
    int nr = r + dr;
    if ((unsigned)nr >= 8u)
        return;
    if (f > 0)
        add_attack(ai, c, make_sq(f - 1, nr));
    if (f < 7)
        add_attack(ai, c, make_sq(f + 1, nr));
}

void gen_slider_attacks(const Position& pos, AttackInfo& ai, Color c, int from, const int* dirs, int ndirs) {
    int f0 = file_of(from), r0 = rank_of(from);
    for (int i = 0; i < ndirs; i++) {
        int df = dirs[2 * i], dr = dirs[2 * i + 1];
        int f = f0 + df, r = r0 + dr;
        while (on_board(f, r)) {
            int sq = make_sq(f, r);
            add_attack(ai, c, sq);
            if (pos.board[sq] != NO_PIECE)
                break;
            f += df;
            r += dr;
        }
    }
}

// Precompute per-square attack counts for both sides.
AttackInfo compute_attacks(const Position& pos) {
    AttackInfo ai{};
    static const int DIR_B[8] = {1, 1, 1, -1, -1, 1, -1, -1};
    static const int DIR_R[8] = {1, 0, -1, 0, 0, 1, 0, -1};

    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;
        Color c = color_of(p);
        PieceType pt = type_of(p);
        switch (pt) {
        case PAWN:
            gen_pawn_attacks(pos, ai, c, sq);
            break;
        case KNIGHT:
            gen_knight_attacks(pos, ai, c, sq);
            break;
        case BISHOP:
            gen_slider_attacks(pos, ai, c, sq, DIR_B, 4);
            break;
        case ROOK:
            gen_slider_attacks(pos, ai, c, sq, DIR_R, 4);
            break;
        case QUEEN:
            gen_slider_attacks(pos, ai, c, sq, DIR_B, 4);
            gen_slider_attacks(pos, ai, c, sq, DIR_R, 4);
            break;
        case KING:
            gen_king_attacks(pos, ai, c, sq);
            break;
        default:
            break;
        }
    }
    return ai;
}

// =====================================================
// Mobility counts
// =====================================================
int mobility_knight(const Position& pos, Color c, int from) {
    static const int df[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int dr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    int f = file_of(from), r = rank_of(from);
    int cnt = 0;
    for (int i = 0; i < 8; i++) {
        int nf = f + df[i], nr = r + dr[i];
        if (!on_board(nf, nr))
            continue;
        int to = make_sq(nf, nr);
        Piece q = pos.board[to];
        if (q == NO_PIECE || color_of(q) != c)
            cnt++;
    }
    return cnt;
}

int mobility_slider(const Position& pos, Color c, int from, const int* dirs, int ndirs) {
    int f0 = file_of(from), r0 = rank_of(from);
    int cnt = 0;
    for (int i = 0; i < ndirs; i++) {
        int df = dirs[2 * i], dr = dirs[2 * i + 1];
        int f = f0 + df, r = r0 + dr;
        while (on_board(f, r)) {
            int to = make_sq(f, r);
            Piece q = pos.board[to];
            if (q == NO_PIECE)
                cnt++;
            else {
                if (color_of(q) != c)
                    cnt++;
                break;
            }
            f += df;
            r += dr;
        }
    }
    return cnt;
}

// Pawn structure helpers.
PawnInfo gather_pawns(const Position& pos) {
    PawnInfo pi{};
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;
        if (type_of(p) != PAWN)
            continue;
        Color c = color_of(p);
        int f = file_of(sq), r = rank_of(sq);
        int ci = (c == WHITE) ? 0 : 1;
        pi.fileCount[ci][f]++;
        pi.ranksMask[ci][f] |= (uint8_t)(1u << r);
    }
    return pi;
}

bool pawn_on(const Position& pos, int sq, Color c) {
    return pos.board[sq] == make_piece(c, PAWN);
}

int low_bit(uint8_t m) {
    for (int r = 0; r < 8; r++)
        if (m & (1u << r))
            return r;
    return -1;
}

// Pawn structure evaluation: isolated/doubled/connected, passers, and king shields.
Score eval_pawns(const Position& pos, const PawnInfo& pi, int kingSqW, int kingSqB, const Weights& W) {
    Score s{0, 0};

    auto eval_color = [&](Color c) {
        int ci = (c == WHITE) ? 0 : 1;
        int oi = (c == WHITE) ? 1 : 0;
        int sign = (c == WHITE) ? +1 : -1;

        // doubled pawns
        for (int f = 0; f < 8; f++) {
            int n = pi.fileCount[ci][f];
            if (n >= 2) {
                int pen = DOUBLED_PAWN_PEN * (n - 1);
                s.mg -= sign * pen;
                s.eg -= sign * pen;
            }
        }

        // per pawn iterate via mask
        for (int f = 0; f < 8; f++) {
            uint8_t mask = pi.ranksMask[ci][f];
            while (mask) {
                int r = low_bit(mask);
                mask &= (uint8_t)(mask - 1);

                // isolated
                bool left = (f > 0 && pi.fileCount[ci][f - 1] > 0);
                bool right = (f < 7 && pi.fileCount[ci][f + 1] > 0);
                if (!left && !right) {
                    s.mg -= sign * ISOLATED_PAWN_PEN;
                    s.eg -= sign * ISOLATED_PAWN_PEN;
                } else {
                    // connected / chain
                    uint8_t near = 0;
                    if (r > 0)
                        near |= (uint8_t)(1u << (r - 1));
                    near |= (uint8_t)(1u << r);
                    if (r < 7)
                        near |= (uint8_t)(1u << (r + 1));
                    bool conn = false;
                    if (f > 0 && (pi.ranksMask[ci][f - 1] & near))
                        conn = true;
                    if (f < 7 && (pi.ranksMask[ci][f + 1] & near))
                        conn = true;
                    if (conn) {
                        s.mg += sign * CONNECTED_PAWN_BONUS;
                        s.eg += sign * CONNECTED_PAWN_BONUS;
                    }

                    // chain: diagonal support by pawn behind
                    bool chain = false;
                    if (c == WHITE) {
                        if (r > 0) {
                            if (f > 0 && (pi.ranksMask[ci][f - 1] & (1u << (r - 1))))
                                chain = true;
                            if (f < 7 && (pi.ranksMask[ci][f + 1] & (1u << (r - 1))))
                                chain = true;
                        }
                    } else {
                        if (r < 7) {
                            if (f > 0 && (pi.ranksMask[ci][f - 1] & (1u << (r + 1))))
                                chain = true;
                            if (f < 7 && (pi.ranksMask[ci][f + 1] & (1u << (r + 1))))
                                chain = true;
                        }
                    }
                    if (chain) {
                        s.mg += sign * CHAIN_PAWN_BONUS;
                        s.eg += sign * CHAIN_PAWN_BONUS;
                    }
                }

                // backward pawn (cheap approximation)
                auto enemyPawnControls = [&](int ff, int rr) -> bool {
                    if (c == WHITE) {
                        int br = rr + 1;
                        if ((unsigned)br < 8u) {
                            if (ff > 0 && pawn_on(pos, make_sq(ff - 1, br), BLACK))
                                return true;
                            if (ff < 7 && pawn_on(pos, make_sq(ff + 1, br), BLACK))
                                return true;
                        }
                    } else {
                        int wr = rr - 1;
                        if ((unsigned)wr < 8u) {
                            if (ff > 0 && pawn_on(pos, make_sq(ff - 1, wr), WHITE))
                                return true;
                            if (ff < 7 && pawn_on(pos, make_sq(ff + 1, wr), WHITE))
                                return true;
                        }
                    }
                    return false;
                };

                int advR = (c == WHITE) ? (r + 1) : (r - 1);
                if ((unsigned)advR < 8u) {
                    bool hasSupport = false;
                    if (c == WHITE) {
                        if (r > 0) {
                            if (f > 0 && (pi.ranksMask[ci][f - 1] & (uint8_t)((1u << r) - 1u)))
                                hasSupport = true;
                            if (f < 7 && (pi.ranksMask[ci][f + 1] & (uint8_t)((1u << r) - 1u)))
                                hasSupport = true;
                        }
                    } else {
                        if (r < 7) {
                            uint8_t above = (uint8_t)(0xFFu & ~((1u << (r + 1)) - 1u));
                            if (f > 0 && (pi.ranksMask[ci][f - 1] & above))
                                hasSupport = true;
                            if (f < 7 && (pi.ranksMask[ci][f + 1] & above))
                                hasSupport = true;
                        }
                    }
                    if (!hasSupport && enemyPawnControls(f, advR)) {
                        s.mg -= sign * BACKWARD_PAWN_PEN;
                        s.eg -= sign * BACKWARD_PAWN_PEN;
                    }
                }

                // passed pawn
                uint8_t aheadMask;
                if (c == WHITE) {
                    aheadMask = (r >= 7) ? 0 : (uint8_t)(0xFFu & ~((1u << (r + 1)) - 1u));
                } else {
                    aheadMask = (r <= 0) ? 0 : (uint8_t)((1u << r) - 1u);
                }

                bool passed = true;
                for (int df = -1; df <= 1; df++) {
                    int ff = f + df;
                    if ((unsigned)ff > 7u)
                        continue;
                    if (pi.ranksMask[oi][ff] & aheadMask) {
                        passed = false;
                        break;
                    }
                }

                if (passed) {
                    int pr = (c == WHITE) ? r : (7 - r);
                    pr = clampi(pr, 0, 7);
                    s.mg += sign * PASSED_MG[pr];
                    s.eg += sign * PASSED_EG[pr];

                    // blocked?
                    int frontSq = (c == WHITE) ? make_sq(f, r + 1) : make_sq(f, r - 1);
                    if ((unsigned)frontSq < 64u && pos.board[frontSq] != NO_PIECE) {
                        s.mg += sign * PASSED_BLOCKED_MG;
                        s.eg += sign * PASSED_BLOCKED_EG;
                    }

                    // protected passer (by pawn)
                    bool prot = false;
                    if (c == WHITE && r > 0) {
                        if (f > 0 && pawn_on(pos, make_sq(f - 1, r - 1), WHITE))
                            prot = true;
                        if (f < 7 && pawn_on(pos, make_sq(f + 1, r - 1), WHITE))
                            prot = true;
                    }
                    if (c == BLACK && r < 7) {
                        if (f > 0 && pawn_on(pos, make_sq(f - 1, r + 1), BLACK))
                            prot = true;
                        if (f < 7 && pawn_on(pos, make_sq(f + 1, r + 1), BLACK))
                            prot = true;
                    }
                    if (prot) {
                        s.mg += sign * PASSED_PROTECTED_MG;
                        s.eg += sign * PASSED_PROTECTED_EG;
                    }

                    // connected passers (cheap)
                    bool connPass = false;
                    if (f > 0) {
                        uint8_t m2 = pi.ranksMask[ci][f - 1];
                        if (m2) {
                            int rr = low_bit(m2);
                            if (std::abs(rr - r) <= 1)
                                connPass = true;
                        }
                    }
                    if (f < 7 && !connPass) {
                        uint8_t m2 = pi.ranksMask[ci][f + 1];
                        if (m2) {
                            int rr = low_bit(m2);
                            if (std::abs(rr - r) <= 1)
                                connPass = true;
                        }
                    }
                    if (connPass) {
                        s.eg += sign * PASSED_CONNECTED_EG;
                    }

                    // outside passer (rough)
                    if (f <= 1 || f >= 6) {
                        s.eg += sign * OUTSIDE_PASSER_EG;
                    }

                    // king distance (EG)
                    int pawnSq = make_sq(f, r);
                    int myK = (c == WHITE) ? kingSqW : kingSqB;
                    int opK = (c == WHITE) ? kingSqB : kingSqW;
                    if (myK >= 0 && opK >= 0) {
                        int dMy = chebyshev(myK, pawnSq);
                        int dOp = chebyshev(opK, pawnSq);
                        s.eg += sign * clampi((dOp - dMy), -4, 4) * 3;
                    }
                }
            }
        }

        // king shield (MG)
        int ksq = (c == WHITE) ? kingSqW : kingSqB;
        if (ksq >= 0) {
            int kf = file_of(ksq), kr = rank_of(ksq);
            bool ks = (kf >= 5), qs = (kf <= 2);
            if (ks || qs) {
                int f0 = ks ? 5 : 0, f1 = ks ? 7 : 2;
                int dr = (c == WHITE) ? +1 : -1;
                int rr = kr + dr;
                if ((unsigned)rr < 8u) {
                    for (int ff = f0; ff <= f1; ff++) {
                        int sq = make_sq(ff, rr);
                        if (pawn_on(pos, sq, c)) {
                            s.mg += sign * SHIELD_PAWN_MG;
                        } else {
                            int miss = SHIELD_MISSING_MG;
                            miss += W.shieldMissingExtraMG;
                            s.mg -= sign * miss;
                        }
                    }
                }
            }
        }
    };

    eval_color(WHITE);
    eval_color(BLACK);
    return s;
}

// King safety based on attack counts and open files.
Score eval_king_safety(const Position& pos, const PawnInfo& pi, const AttackInfo& ai, int kingSqW,
                       int kingSqB, int phase, const Weights& W) {
    Score s{0, 0};
    if (phase < 96)
        return s;

    auto eval_defender = [&](Color def) {
        Color atk = flip_color(def);
        int di = (def == WHITE) ? 0 : 1;
        int aiC = (atk == WHITE) ? 0 : 1;
        int sign = (def == WHITE) ? +1 : -1;

        int ksq = (def == WHITE) ? kingSqW : kingSqB;
        if (ksq < 0)
            return;

        int kf = file_of(ksq), kr = rank_of(ksq);
        int ringAtt = 0;
        int ringAtkPieces = 0;

        for (int df = -1; df <= 1; df++)
            for (int dr = -1; dr <= 1; dr++) {
                if (!df && !dr)
                    continue;
                int nf = kf + df, nr = kr + dr;
                if (!on_board(nf, nr))
                    continue;
                int sq = make_sq(nf, nr);
                ringAtt += ai.attacks[aiC][sq];
                ringAtkPieces += ai.attackers[aiC][sq];
            }

        int openScore = 0;
        for (int ff = kf - 1; ff <= kf + 1; ff++) {
            if ((unsigned)ff > 7u)
                continue;
            bool defPawn = pi.fileCount[di][ff] > 0;
            bool atkPawn = pi.fileCount[aiC][ff] > 0;
            if (!defPawn) {
                openScore += atkPawn ? W.ksSemiOpenMG : W.ksOpenFileMG;
            }
        }

        int danger = ringAtt * W.ksAttackWeight + ringAtkPieces * W.ksAttackerWeight + openScore;

        bool wQ = false, bQ = false;
        for (int sq = 0; sq < 64; sq++) {
            Piece p = pos.board[sq];
            if (p == NO_PIECE)
                continue;
            if (type_of(p) == QUEEN) {
                if (color_of(p) == WHITE)
                    wQ = true;
                else
                    bQ = true;
            }
        }
        if (!(wQ && bQ))
            danger = (danger * 2) / 3;

        danger = (danger * W.ksScalePct) / 100;

        s.mg -= sign * danger;
    };

    eval_defender(WHITE);
    eval_defender(BLACK);
    return s;
}

// Piece activity and mobility (rooks, minors, queen).
Score eval_pieces(const Position& pos, const PawnInfo& pi, const AttackInfo& /*ai*/, int /*kingSqW*/,
                  int /*kingSqB*/) {
    Score s{0, 0};

    static const int DIR_B[8] = {1, 1, 1, -1, -1, 1, -1, -1};
    static const int DIR_R[8] = {1, 0, -1, 0, 0, 1, 0, -1};

    int bishopCount[2] = {0, 0};
    int rookSq[2][2] = {{-1, -1}, {-1, -1}};
    int rookN[2] = {0, 0};

    auto clear_between_rank = [&](int a, int b) -> bool {
        if (rank_of(a) != rank_of(b))
            return false;
        int ra = rank_of(a), fa = file_of(a), fb = file_of(b);
        int step = (fa < fb) ? 1 : -1;
        for (int f = fa + step; f != fb; f += step) {
            if (pos.board[make_sq(f, ra)] != NO_PIECE)
                return false;
        }
        return true;
    };
    auto clear_between_file = [&](int a, int b) -> bool {
        if (file_of(a) != file_of(b))
            return false;
        int fa = file_of(a), ra = rank_of(a), rb = rank_of(b);
        int step = (ra < rb) ? 1 : -1;
        for (int r = ra + step; r != rb; r += step) {
            if (pos.board[make_sq(fa, r)] != NO_PIECE)
                return false;
        }
        return true;
    };

    auto is_outpost = [&](Color c, int sq) -> bool {
        int f = file_of(sq), r = rank_of(sq);
        if (c == WHITE && r < 3)
            return false;
        if (c == BLACK && r > 4)
            return false;

        bool prot = false;
        if (c == WHITE && r > 0) {
            if (f > 0 && pawn_on(pos, make_sq(f - 1, r - 1), WHITE))
                prot = true;
            if (f < 7 && pawn_on(pos, make_sq(f + 1, r - 1), WHITE))
                prot = true;
        }
        if (c == BLACK && r < 7) {
            if (f > 0 && pawn_on(pos, make_sq(f - 1, r + 1), BLACK))
                prot = true;
            if (f < 7 && pawn_on(pos, make_sq(f + 1, r + 1), BLACK))
                prot = true;
        }
        if (!prot)
            return false;

        Color opp = flip_color(c);
        auto enemyPawnCouldChase = [&](int ff) -> bool {
            if ((unsigned)ff > 7u)
                return false;
            uint8_t m = pi.ranksMask[(opp == WHITE) ? 0 : 1][ff];
            if (!m)
                return false;
            if (opp == WHITE) {
                for (int rr = 0; rr <= r - 2; rr++)
                    if (m & (1u << rr))
                        return true;
            } else {
                for (int rr = 7; rr >= r + 2; rr--)
                    if (m & (1u << rr))
                        return true;
            }
            return false;
        };
        if (enemyPawnCouldChase(f - 1) || enemyPawnCouldChase(f + 1))
            return false;
        return true;
    };

    auto bishop_color_pen = [&](Color c, int bishopSq) -> Score {
        int ci = (c == WHITE) ? 0 : 1;
        int sign = (c == WHITE) ? +1 : -1;
        bool dark = ((file_of(bishopSq) + rank_of(bishopSq)) & 1) == 1;
        int cnt = 0;
        for (int f = 0; f < 8; f++) {
            uint8_t m = pi.ranksMask[ci][f];
            while (m) {
                int r = low_bit(m);
                m &= (uint8_t)(m - 1);
                bool pdark = ((f + r) & 1) == 1;
                if (pdark == dark)
                    cnt++;
            }
        }
        int pen = clampi(cnt - 4, 0, 6);
        return {-sign * pen * BAD_BISHOP_MG, -sign * pen * BAD_BISHOP_EG};
    };

    auto early_queen_pen = [&](Color c, int sq) -> int {
        if (c == WHITE) {
            if (sq != D1)
                return EARLY_QUEEN_PEN_MG;
        } else {
            if (sq != D8)
                return EARLY_QUEEN_PEN_MG;
        }
        return 0;
    };

    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;
        Color c = color_of(p);
        int sign = (c == WHITE) ? +1 : -1;
        int sqq = (c == WHITE) ? sq : mirror_sq(sq);
        PieceType pt = type_of(p);

        Score base{sign * MG_VAL[(int)pt], sign * EG_VAL[(int)pt]};
        switch (pt) {
        case PAWN:
            base.mg += sign * PST_P_MG[sqq];
            base.eg += sign * PST_P_EG[sqq];
            break;
        case KNIGHT:
            base.mg += sign * PST_N_MG[sqq];
            base.eg += sign * PST_N_EG[sqq];
            break;
        case BISHOP:
            base.mg += sign * PST_B_MG[sqq];
            base.eg += sign * PST_B_EG[sqq];
            break;
        case ROOK:
            base.mg += sign * PST_R_MG[sqq];
            base.eg += sign * PST_R_EG[sqq];
            break;
        case QUEEN:
            base.mg += sign * PST_Q_MG[sqq];
            base.eg += sign * PST_Q_EG[sqq];
            break;
        case KING:
            base.mg += sign * PST_K_MG[sqq];
            base.eg += sign * PST_K_EG[sqq];
            break;
        default:
            break;
        }
        s += base;

        if (pt == KNIGHT) {
            int mob = mobility_knight(pos, c, sq);
            s.mg += sign * mob * MOB_N_MG;
            s.eg += sign * mob * MOB_N_EG;

            int f = file_of(sq);
            if (f == 0 || f == 7) {
                s.mg -= sign * KNIGHT_RIM_PEN_MG;
                s.eg -= sign * KNIGHT_RIM_PEN_EG;
            }

            if (is_outpost(c, sq)) {
                s.mg += sign * OUTPOST_MG;
                s.eg += sign * OUTPOST_EG;
            }
        } else if (pt == BISHOP) {
            int mob = mobility_slider(pos, c, sq, DIR_B, 4);
            s.mg += sign * mob * MOB_B_MG;
            s.eg += sign * mob * MOB_B_EG;

            s += bishop_color_pen(c, sq);
            bishopCount[(c == WHITE) ? 0 : 1]++;
        } else if (pt == ROOK) {
            int mob = mobility_slider(pos, c, sq, DIR_R, 4);
            s.mg += sign * mob * MOB_R_MG;
            s.eg += sign * mob * MOB_R_EG;

            int ci = (c == WHITE) ? 0 : 1;
            if (rookN[ci] < 2)
                rookSq[ci][rookN[ci]++] = sq;

            int f = file_of(sq);
            bool ownPawn = pi.fileCount[ci][f] > 0;
            bool oppPawn = pi.fileCount[ci ^ 1][f] > 0;
            if (!ownPawn && !oppPawn)
                s.mg += sign * ROOK_OPEN_FILE_MG;
            else if (!ownPawn && oppPawn)
                s.mg += sign * ROOK_SEMIOPEN_FILE_MG;

            int r = rank_of(sq);
            if (c == WHITE && r == 6)
                s.mg += sign * ROOK_7TH_MG;
            if (c == BLACK && r == 1)
                s.mg += sign * ROOK_7TH_MG;
        } else if (pt == QUEEN) {
            int mob = mobility_slider(pos, c, sq, DIR_B, 4) + mobility_slider(pos, c, sq, DIR_R, 4);
            s.mg += sign * mob * MOB_Q_MG;
            s.eg += sign * mob * MOB_Q_EG;

            s.mg -= sign * early_queen_pen(c, sq);
        } else if (pt == PAWN) {
            int f = file_of(sq), r = rank_of(sq);
            int dr = (c == WHITE) ? +1 : -1;
            int nr = r + dr;
            if ((unsigned)nr < 8u) {
                if (f > 0) {
                    int to = make_sq(f - 1, nr);
                    if (to == D4 || to == E4 || to == D5 || to == E5)
                        s.mg += sign * CENTER_CONTROL_MG;
                }
                if (f < 7) {
                    int to = make_sq(f + 1, nr);
                    if (to == D4 || to == E4 || to == D5 || to == E5)
                        s.mg += sign * CENTER_CONTROL_MG;
                }
            }
        }
    }

    if (bishopCount[0] >= 2) {
        s.mg += BISHOP_PAIR_MG;
        s.eg += BISHOP_PAIR_EG;
    }
    if (bishopCount[1] >= 2) {
        s.mg -= BISHOP_PAIR_MG;
        s.eg -= BISHOP_PAIR_EG;
    }

    for (int ci = 0; ci < 2; ci++) {
        if (rookSq[ci][0] >= 0 && rookSq[ci][1] >= 0) {
            int a = rookSq[ci][0], b = rookSq[ci][1];
            bool conn = false;
            if (rank_of(a) == rank_of(b))
                conn = clear_between_rank(a, b);
            else if (file_of(a) == file_of(b))
                conn = clear_between_file(a, b);
            if (conn) {
                int sign = (ci == 0) ? +1 : -1;
                s.mg += sign * ROOK_CONNECTED_MG;
            }
        }
    }

    return s;
}

// Main evaluation entry: blends MG/EG by phase and applies tempo.
int evaluate(const Position& pos) {
    int kingW = -1, kingB = -1;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.board[sq];
        if (p == NO_PIECE)
            continue;
        if (type_of(p) == KING) {
            if (color_of(p) == WHITE)
                kingW = sq;
            else
                kingB = sq;
        }
    }

    int phase = game_phase_256(pos);

    PawnInfo pi = gather_pawns(pos);
    AttackInfo ai = compute_attacks(pos);

    const Weights W = weights_for(pos.side);

    Score total{0, 0};

    // ===== 以后你新增评估项，就统一在这里累计 =====
    total += eval_pieces(pos, pi, ai, kingW, kingB);
    total += eval_pawns(pos, pi, kingW, kingB, W);
    total += eval_king_safety(pos, pi, ai, kingW, kingB, phase, W);

    int score = (total.mg * phase + total.eg * (256 - phase)) / 256;

    if (phase > 120) {
        score += (pos.side == WHITE ? +W.tempoMG : -W.tempoMG);
    }

    // 约定：evaluate 返回“当前行棋方视角”的 cp
    return (pos.side == WHITE) ? score : -score;
}

} // namespace eval