#include "Zobrist.h"

namespace LC {

std::shared_ptr<const Zobrist> Zobrist::m_pInstance = nullptr;

Zobrist::Zobrist() {
    initRandomKeys();
}

std::shared_ptr<const Zobrist> Zobrist::getInstance() {
    if(m_pInstance == nullptr) return m_pInstance = std::shared_ptr<const Zobrist>(new Zobrist());

    return m_pInstance;
}

// Call this once to initialize the random hash values
void Zobrist::initRandomKeys() {
    std::mt19937_64 rng(0xABCDEF1234567890); // fixed seed for reproducibility
    std::uniform_int_distribution<uint64_t> dist;

    for (auto& pieceArray : pieceHash) {
        for (auto& square : pieceArray) {
            square = dist(rng);
        }
    }

    for (auto& val : castlingHash) {
        val = dist(rng);
    }

    for (auto& val : enPassantHash) {
        val = dist(rng);
    }

    sideToMoveHash = dist(rng);
}

// Compute the Zobrist hash for a given position
uint64_t Zobrist::computeHash(Piece board[8][8], bool whiteToMove, int castlingRights, int enPassantFile) const {
    uint64_t hash = 0;

    for (int i = 0; i<8; i++) {
        for(int j = 0; j<8; j++) {    
            int square = i*8 + j;            
            Piece piece = board[i][j];
            if (piece == Piece::EMPTY) continue; // empty square

            int index = (int)piece;
            
            hash ^= pieceHash[index][square];
        }
    }

    if (whiteToMove)
        hash ^= sideToMoveHash;

    if (castlingRights >= 0 && castlingRights < 16)
        hash ^= castlingHash[castlingRights];

    if (enPassantFile >= 0 && enPassantFile < 8)
        hash ^= enPassantHash[enPassantFile];

    return hash;
}

};
