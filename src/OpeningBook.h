#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "types.h"
#include "Position.h"

namespace openingbook {

struct Entry {
    uint64_t key = 0;
    uint16_t rawMove = 0;
    uint16_t weight = 0;
    uint32_t learn = 0;
};

// 字节序转换
uint16_t bswap16(uint16_t x);
uint32_t bswap32(uint32_t x);
uint64_t bswap64(uint64_t x);

// Polyglot 相关
int poly_piece_index(Piece p);
uint64_t polyglot_hash(const Position& pos);
Move decode_raw_to_legal(const Position& pos, uint16_t raw);

// 开局库类
class Book {
public:
    bool load(const std::string& path);
    bool loaded() const;

    Move best_move(const Position& pos, uint16_t* outWeight = nullptr) const;
    std::vector<Move> build_pv(Position pos, Move first, int maxPly = 16) const;

private:
    std::pair<size_t, size_t> equal_range(uint64_t key) const;

private:
    bool loaded_ = false;
    std::vector<Entry> entries_;
};

// 走法转 UCI 字符串
std::string move_to_uci_local(Move m);

} // namespace openingbook