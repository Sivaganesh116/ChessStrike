#ifndef __HELPER_H__
#define __HELPER_H__

#include <cstdint>
#include <algorithm>

#include "Board.h"

namespace LC {

extern uint64_t knightAttackSquares[64];
extern uint64_t bishopAttackSquares[64];
extern uint64_t rookAttackSquares[64];
extern uint64_t kingAttackSquares[64];

extern uint64_t rangeMasks[64][64];

extern std::vector<std::vector<uint64_t>> rookAttacksForOccupancy, bishopAttacksForOccupancy;

enum class PinDirection {
    NONE,
    RANK,
    FILE,
    TOP_BOTTOM_DIAG,
    BOTTOM_TOP_DIAG
};

class Board;
enum class CheckType;

void compute();
PinDirection getPinDirection(bool white, int pieceSquare, Board & board, bool updateDiscoveryCheckSquare);
uint64_t getBishopAttacksForSquareAndOccupancy(int square, uint64_t occupancy);
uint64_t getRookAttacksForSquareAndOccupancy(int square, uint64_t occupancy);
uint64_t getQueenAttacksForSquareAndOccupancy(int square, uint64_t occupancy);
uint64_t generateLegalAttacksForColor(bool white, bool checkPins, bool includeKing, bool includePawnMoves, const Board& board);
bool isKingUnderCheck(bool white, const Board& board);
bool canAnyPieceMove(bool white, const Board& board);
bool isKingInSameRay(int pieceSquare, int kingSquare, int newKingSquare, Piece attackingPiece);
void calculateMoveResult(CheckType check, uint64_t positionHash, bool isWhiteTurn, Board& board);
bool doesColorHaveInsufficientMaterial(bool white, const Board& board);

};

#endif
