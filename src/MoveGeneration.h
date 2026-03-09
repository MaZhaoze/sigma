#pragma once
#include <vector>
#include <cmath>

#include "types.h"
#include "Position.h"
#include "Attack.h"

namespace movegen {

inline bool on_board(int sq) {
    return sq >= 0 && sq < 64;
}

inline bool same_rank(int a, int b) {
    return rank_of(a) == rank_of(b);
}

inline bool diag_step_ok(int from, int to) {
    return std::abs(file_of(to) - file_of(from)) == 1;
}

void push_move(const Position& pos, std::vector<Move>& moves, int from, int to, int flags = 0, int promo = 0);
void add_promo(const Position& pos, std::vector<Move>& moves, int from, int to, int flags = 0);
void generate_pseudo_legal(const Position& pos, std::vector<Move>& moves);
bool legal_castle_path_ok(const Position& pos, Move m);
void generate_legal(Position& pos, std::vector<Move>& legal);
void generate_legal_captures(Position& pos, std::vector<Move>& caps);

} // namespace movegen