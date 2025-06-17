#ifndef __ZOBRIST_H__
#define __ZOBRIST_H__

#include <array>
#include <random>
#include <cstdint>
#include <iostream>

#include "Board.h"

namespace LC {

enum class Piece;

class Zobrist {
private:
    static constexpr int NUM_SQUARES = 64;
    static constexpr int NUM_PIECE_TYPES = 12; // P, N, B, R, Q, K, p, n, b, r, q, k

    std::array<std::array<uint64_t, NUM_SQUARES>, NUM_PIECE_TYPES> pieceHash;
    std::array<uint64_t, 16> castlingHash; // 16 combinations of castling rights
    std::array<uint64_t, 8> enPassantHash; // Files a-h (if applicable)
    uint64_t sideToMoveHash;

    static std::shared_ptr<const Zobrist> m_pInstance;

    Zobrist();

public:
    static std::shared_ptr<const Zobrist> getInstance();

    // Call this once to initialize the random hash values
    void initRandomKeys();

    // Compute the Zobrist hash for a given position
    uint64_t computeHash(Piece board[8][8], bool whiteToMove, int castlingRights, int enPassantFile) const;
};

}

#endif
