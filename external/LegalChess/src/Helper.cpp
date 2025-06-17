#include "Helper.h"
#include "Board.h"

namespace LC {

static int knightMoveOffsets[8][2] = {{-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}, {-2, -1}, {-2, 1}};
static int kingMoveOffsets[8][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

uint64_t knightAttackSquares[64];
uint64_t bishopAttackSquares[64];
uint64_t rookAttackSquares[64];
uint64_t kingAttackSquares[64];

uint64_t rangeMasks[64][64];

std::vector<std::vector<uint64_t>> rookAttacksForOccupancy(64), bishopAttacksForOccupancy(64);

int power(int base, int exp) {
    if(exp == 0) return 1;
    if(exp == 1) return base;
    if(exp == 2) return base*base;

    if(exp % 2) return base * power(base, exp-1);
    return power(base, exp/2) * power(base, exp/2);
}

void compute() {
    // calculate range masks
    for(int sq = 0; sq < 64; sq++) {
        // rank
        uint64_t rankMask = (1ULL << sq);
        rangeMasks[sq][sq] = rankMask;

        int row = sq/8, col = sq%8;
        int rankSq = sq + 1;

        while(rankSq < (row+1)*8) {
            rankMask |= (1ULL << rankSq);
            rangeMasks[sq][rankSq] = rangeMasks[rankSq][sq] = rankMask;
            rankSq++;
        }

        // file
        uint64_t fileMask = (1ULL << sq);
        
        int fileSq = sq + 8;

        while(fileSq < 64) {
            fileMask |= (1ULL << fileSq);
            rangeMasks[sq][fileSq] = rangeMasks[fileSq][sq] = fileMask;
            fileSq += 8;
        }

        // top To Bottom Diag
        int numSquares = std::min(8-row-1, 8-col-1);
        int topToBottomSquare = sq + 9;
        uint64_t topToBottomMask = (1ULL << sq);

        while(numSquares--) {
            topToBottomMask |= (1ULL << topToBottomSquare);
            rangeMasks[sq][topToBottomSquare] = rangeMasks[topToBottomSquare][sq] = topToBottomMask;
            topToBottomSquare += 9;
        }

        // bottom To Top Diag
        numSquares = std::min(8-row-1, col);
        int bottomToTopSquare = sq + 7;
        uint64_t bottomToTopMask = (1ULL << sq);

        while(numSquares--) {
            bottomToTopMask |= (1ULL << bottomToTopSquare);
            rangeMasks[sq][bottomToTopSquare] = rangeMasks[bottomToTopSquare][sq] = bottomToTopMask;
            bottomToTopSquare += 7;
        }
    }


    // calculate attack squares for pieces on each square
    for(int sq = 0; sq < 64; sq++) {
        int row = sq/8, col = sq%8;

        // knight attacks        
        uint64_t knightAttacks = 0;

        for(auto & offSet : knightMoveOffsets) {
            int newRow = row + offSet[0], newCol = col + offSet[1];

            if(newRow >= 0 && newRow < 8 && newCol >= 0 && newCol < 8) {
                int newSquare = newRow*8 + newCol;

                knightAttacks |= (1ULL << newSquare);
            }
        }

        knightAttackSquares[sq] = knightAttacks;

        // king attacks
        uint64_t kingAttacks = 0;

        for(auto & offSet : kingMoveOffsets) {
            int newRow = row + offSet[0], newCol = col + offSet[1];

            if(newRow >= 0 && newRow < 8 && newCol >= 0 && newCol < 8) {
                int newSquare = newRow*8 + newCol;

                kingAttacks |= (1ULL << newSquare);
            }
        }

        kingAttackSquares[sq] = kingAttacks;

        // rook Attacks
        uint64_t rookAttacks = (rangeMasks[row*8][row*8+7] | rangeMasks[col][56+col]);
        rookAttacks &= ~(1ULL << sq);

        rookAttackSquares[sq] = rookAttacks;

        // bishop Attacks
        uint64_t bishopAttacks = 0;

        // top to bottom diag
        int topLeftSquare = sq - (9 * std::min(row, col));
        int bottomRightSquare = sq + (9 * std::min(8-row-1, 8-col-1));

        bishopAttacks |= rangeMasks[topLeftSquare][bottomRightSquare];

        // bottom to top diag
        int bottomLeftSquare = sq + (7 * std::min(8-row-1, col));
        int topRightSquare = sq - (7 * std::min(row, 8-col-1));

        bishopAttacks |= rangeMasks[bottomLeftSquare][topRightSquare];

        bishopAttackSquares[sq] = bishopAttacks;
    }

    // rookAttacksForOccupancy.resize(64);
    // bishopAttacksForOccupancy.resize(64);

    // calculate rook attacks for occupancy
    for(int sq = 0; sq<64; sq++) {
        int row = sq/8, col = sq%8;

        uint64_t allAttacksForSquare = rookAttackSquares[sq];

        int numSquares = __builtin_popcountll(allAttacksForSquare);
        int totalConfigs = power(2, numSquares);

        rookAttacksForOccupancy[sq].resize(totalConfigs);

        for(int config = 0; config < totalConfigs; config++) {
            // derive occupancy mask basedon config
            uint64_t occupancyMask = 0;

            uint64_t allAttacksCopy = allAttacksForSquare;

            int configBitPos = 0;

            while(allAttacksCopy) {
                int attackSquare = __builtin_ctzll(allAttacksCopy);
                allAttacksCopy &= allAttacksCopy - 1;

                if(((1 << configBitPos) & config) != 0) occupancyMask |= (1ULL << attackSquare);

                configBitPos++;
            }

            uint64_t legalAttacks = 0;

            // calculate the legal moves from the sq in rank and file, including the first blocker in the occupancy mask
            // left
            int legalAttackSquare = sq - 1;
            while(legalAttackSquare >= row*8) {
                legalAttacks |= (1ULL << legalAttackSquare);

                if(((1ULL << legalAttackSquare) & occupancyMask) != 0) break;

                legalAttackSquare--;
            }

            // right
            legalAttackSquare = sq + 1;
            while(legalAttackSquare <= row*8 + 7) {
                legalAttacks |= (1ULL << legalAttackSquare);

                if(((1ULL << legalAttackSquare) & occupancyMask) != 0) break;

                legalAttackSquare++;
            }

            // top
            legalAttackSquare = sq - 8;
            while(legalAttackSquare >= 0) {
                legalAttacks |= (1ULL << legalAttackSquare);

                if(((1ULL << legalAttackSquare) & occupancyMask) != 0) break;

                legalAttackSquare -= 8;
            }

            // bottom
            legalAttackSquare = sq + 8;
            while(legalAttackSquare < 64) {
                legalAttacks |= (1ULL << legalAttackSquare);

                if(((1ULL << legalAttackSquare) & occupancyMask) != 0) break;

                legalAttackSquare += 8;
            }

            // store the legal attacks for square and for occupancy mask
            rookAttacksForOccupancy[sq][config] = legalAttacks;
        }
    }


    // calculate bishop attack for occupancy
    for(int sq = 0; sq < 64; sq++) {
        int row = sq/8, col = sq%8;

        uint64_t allAttacksForSquare = bishopAttackSquares[sq];

        int numSquares = __builtin_popcountll(allAttacksForSquare);
        int totalConfigs = power(2, numSquares);

        bishopAttacksForOccupancy[sq].resize(totalConfigs);

        for(int config = 0; config < totalConfigs; config++) {
            // derive occupancy mask basedon config
            uint64_t occupancyMask = 0;

            uint64_t allAttacksCopy = allAttacksForSquare;

            int configBitPos = 0;

            while(allAttacksCopy) {
                int attackSquare = __builtin_ctzll(allAttacksCopy);
                allAttacksCopy &= allAttacksCopy - 1;

                if(((1 << configBitPos) & config) != 0) occupancyMask |= (1ULL << attackSquare);

                configBitPos++;
            }

            uint64_t legalAttacks = 0;

            // calculate the legal moves from the sq in both diagonals, including the first blocker in the occupancy mask 
            // top left
            int topLeftSquare = sq - (9 * std::min(row, col));
            int newSquare = sq - 9;

            while(newSquare >= topLeftSquare) {
                legalAttacks |= (1ULL << newSquare);

                if(((1ULL << newSquare) & occupancyMask) != 0) break;

                newSquare -= 9;
            }
            
            // bottom right
            int bottomRightSquare = sq + (9 * std::min(8-row-1, 8-col-1));
            newSquare = sq + 9;

            while(newSquare <= bottomRightSquare) {
                legalAttacks |= (1ULL << newSquare);

                if(((1ULL << newSquare) & occupancyMask) != 0) break;

                newSquare += 9;
            }
            
            // bottom left
            int bottomLeftSquare = sq + (7 * std::min(8-row-1, col));
            newSquare = sq + 7;

            while(newSquare <= bottomLeftSquare) {
                legalAttacks |= (1ULL << newSquare);

                if(((1ULL << newSquare) & occupancyMask) != 0) break;

                newSquare += 7;
            }

            // top right
            int topRightSquare = sq - (7 * std::min(row, 8-col-1));
            newSquare = sq - 7;

            while(newSquare >= topRightSquare) {
                legalAttacks |= (1ULL << newSquare);

                if(((1ULL << newSquare) & occupancyMask) != 0) break;

                newSquare -= 7;
            }

            bishopAttacksForOccupancy[sq][config] = legalAttacks;
        }
    }
}

// struct PrecomputeInit {
//     PrecomputeInit() {
//         compute();
//     }
// };

// Make an object of PrecomputeInit to run it before main
// static PrecomputeInit obj;


PinDirection getPinDirection(bool white, int pieceSquare, Board & board, bool updateDiscoveryCheckSquare) {
    int rank = pieceSquare / 8;
    int file = pieceSquare % 8;

    uint8_t kingSquare = __builtin_ctzll(board.getPieceBitBoard(white ? Piece::WHITE_KING : Piece::BLACK_KING));

    uint8_t ki = kingSquare / 8, kj = kingSquare % 8;

    uint64_t inBetweenMask = rangeMasks[pieceSquare][kingSquare];
    inBetweenMask &= ~(1ULL << kingSquare);
    inBetweenMask &= ~(1ULL << pieceSquare);

    // if there is a piece between king square and piece square
    if(inBetweenMask & board.getAllPiecesBitBoard()) return PinDirection::NONE;

    int borderSquare;
    PinDirection direction = PinDirection::NONE;
    
    // aligned in diagonal
    if(abs(ki - rank) == abs(kj - file)) {
        // topLeft
        if(ki > rank && kj > file) {
            borderSquare = -(std::min(rank, file) * 9) + pieceSquare;
            direction = PinDirection::TOP_BOTTOM_DIAG;
        }

        // topRight
        else if(ki > rank && kj < file) {
            borderSquare = -(std::min(rank, 8-file-1) * 7) + pieceSquare;
            direction = PinDirection::BOTTOM_TOP_DIAG;
        }

        // bottomRight
        else if(ki < rank && kj < file) {
            borderSquare = (std::min(8 - rank - 1, 8 - file - 1) * 9) + pieceSquare;
            direction = PinDirection::BOTTOM_TOP_DIAG;
        }

        // bottomLeft
        else {
            borderSquare = (std::min(8 - rank - 1, file) * 7) + pieceSquare;
            direction = PinDirection::TOP_BOTTOM_DIAG;
        }

        // if piece is at an edge, it can't be pinned
        if(borderSquare == pieceSquare) return PinDirection::NONE;

        uint64_t beyondMask = rangeMasks[pieceSquare][borderSquare];
        beyondMask &= ~(1ULL << pieceSquare);

        // obstructing pieces are all same color pieces and attacking color rooks, knights, pawns and king (opponent bishops and queens attack)
        uint64_t attackingBishops = board.getPieceBitBoard(white ? Piece::BLACK_BISHOP : Piece::WHITE_BISHOP);
        uint64_t attackingQueens = board.getPieceBitBoard(white ? Piece::BLACK_QUEEN : Piece::WHITE_QUEEN);
        uint64_t obstructingPieces = board.getColorBitBoard(white) | 
                                     (board.getColorBitBoard(!white) & ~(attackingBishops | attackingQueens));

        obstructingPieces &= beyondMask;

        // Now OR opponent Bishops and Queens
        uint64_t attackingPieces = (attackingBishops | attackingQueens);
        attackingPieces &= beyondMask;
        
        // if top left or top right
        if((ki > rank && kj > file) || (ki > rank && kj < file)) {
            int obstructingMSBBit = obstructingPieces ? __builtin_clzll(obstructingPieces) : 64;
            int attackingMSBBit = attackingPieces ? __builtin_clzll(attackingPieces) : 64;

            // the piece is not pinned
            if(obstructingMSBBit <= attackingMSBBit) return PinDirection::NONE;  
            
            // piece is pinned, if the caller intends to find a discovery check, update
            if(updateDiscoveryCheckSquare) board.updateDiscoveryCheckSquare(attackingMSBBit);
        }
        // if bottom left or bottom right
        else {
            int obstructingLSBBit = obstructingPieces ? __builtin_ctzll(obstructingPieces) : 64;
            int attackingLSBBit = attackingPieces ? __builtin_ctzll(attackingPieces) : 64;

            // the piece is not pinned
            if(obstructingLSBBit <= attackingLSBBit) return PinDirection::NONE; 

            // piece is pinned, if the caller intends to find a discovery check, update
            if(updateDiscoveryCheckSquare) board.updateDiscoveryCheckSquare(attackingLSBBit);
        }
    }
    // aligned in File
    else if(kj == file) {
        direction = PinDirection::FILE;

        // top
        if(ki > rank) borderSquare = file;
        // bottom
        else borderSquare = 56 + file;

        // if piece is on edge, it cannot be pinned
        if(pieceSquare == borderSquare) return PinDirection::NONE;

        uint64_t beyondMask = rangeMasks[pieceSquare][borderSquare];
        beyondMask &= ~(1ULL << pieceSquare);

        // obstructing pieces are all same color pieces and attacking color bishops, knights, pawns and king (opponent rooks and queens attack)
        uint64_t attackingRooks = board.getPieceBitBoard(white ? Piece::BLACK_ROOK : Piece::WHITE_ROOK);
        uint64_t attackingQueens = board.getPieceBitBoard(white ? Piece::BLACK_QUEEN : Piece::WHITE_QUEEN);
        uint64_t obstructingPieces = board.getColorBitBoard(white) | 
                                     (board.getColorBitBoard(!white) & ~(attackingRooks | attackingQueens));

        obstructingPieces &= beyondMask;
        
        // OR opponent queens and rooks
        uint64_t attackingPieces = (attackingRooks | attackingQueens);
        attackingPieces &= beyondMask;

        // top
        if(ki > rank) {
            int obstructingMSBBit = obstructingPieces ? __builtin_clzll(obstructingPieces) : 64;
            int attackingMSBBit = attackingPieces ? __builtin_clzll(attackingPieces) : 64;

            if(obstructingMSBBit <= attackingMSBBit) return PinDirection::NONE;

            // piece is pinned, if the caller intends to find a discovery check, update
            if(updateDiscoveryCheckSquare) board.updateDiscoveryCheckSquare(attackingMSBBit);
        }
        // bottom
        else {
            int obstructingLSBBit = obstructingPieces ? __builtin_ctzll(obstructingPieces) : 64;
            int attackingLSBBit = attackingPieces ? __builtin_ctzll(attackingPieces) : 64;

            if(obstructingLSBBit <= attackingLSBBit) return PinDirection::NONE;

            // piece is pinned, if the caller intends to find a discovery check, update
            if(updateDiscoveryCheckSquare) board.updateDiscoveryCheckSquare(attackingLSBBit);
        }
    }
    // aligned in rank
    else if(ki == rank) {
        direction = PinDirection::RANK;

        // left
        if(kj > file) borderSquare = rank*8;
        // bottom
        else borderSquare = rank*8 + 7;

        // if piece is on edge, it cannot be pinned
        if(pieceSquare == borderSquare) return PinDirection::NONE;

        uint64_t beyondMask = rangeMasks[pieceSquare][borderSquare];
        beyondMask &= ~(1ULL << pieceSquare);

        // obstructing pieces are all same color pieces and attacking color bishops, knights, pawns and king (opponent rooks and queens attack)
        uint64_t attackingRooks = board.getPieceBitBoard(white ? Piece::BLACK_ROOK : Piece::WHITE_ROOK);
        uint64_t attackingQueens = board.getPieceBitBoard(white ? Piece::BLACK_QUEEN : Piece::WHITE_QUEEN);
        uint64_t obstructingPieces = board.getColorBitBoard(white) | 
                                     (board.getColorBitBoard(!white) & ~(attackingRooks | attackingQueens));
        
        obstructingPieces &= beyondMask;

        // OR opponent queens and rooks
        uint64_t attackingPieces = (attackingRooks | attackingQueens);
        attackingPieces &= beyondMask;

        // left
        if(kj > file) {
            int obstructingMSBBit = obstructingPieces ? __builtin_clzll(obstructingPieces) : 64;
            int attackingMSBBit = attackingPieces ? __builtin_clzll(attackingPieces) : 64;

            if(obstructingMSBBit <= attackingMSBBit) return PinDirection::NONE;

            // piece is pinned, if the caller intends to find a discovery check, update
            if(updateDiscoveryCheckSquare) board.updateDiscoveryCheckSquare(attackingMSBBit);
        }
        // bottom
        else {
            int obstructingLSBBit = obstructingPieces ? __builtin_ctzll(obstructingPieces) : 64;
            int attackingLSBBit = attackingPieces ? __builtin_ctzll(attackingPieces) : 64;

            if(obstructingLSBBit <= attackingLSBBit) return PinDirection::NONE;

            // piece is pinned, if the caller intends to find a discovery check, update
            if(updateDiscoveryCheckSquare) board.updateDiscoveryCheckSquare(attackingLSBBit);
        }
    }


    return direction;
}


uint64_t getBishopAttacksForSquareAndOccupancy(int square, uint64_t occupancy) {
    int configIndex = 0;
    uint64_t attacksFromSquare = bishopAttackSquares[square];
    uint64_t occupancyMask = (occupancy & attacksFromSquare);

    int configBitPos = 0;
    while(attacksFromSquare) {
        int attackSquare = __builtin_ctzll(attacksFromSquare);
        attacksFromSquare &= attacksFromSquare - 1;

        if(((1ULL << attackSquare) & occupancyMask) != 0) {
            configIndex |= (1 << configBitPos);
        }

        configBitPos++;
    }

    return bishopAttacksForOccupancy[square][configIndex];
}

uint64_t getRookAttacksForSquareAndOccupancy(int square, uint64_t occupancy) {
    int configIndex = 0;
    uint64_t attacksFromSquare = rookAttackSquares[square];
    uint64_t occupancyMask = (occupancy & attacksFromSquare);

    int configBitPos = 0;
    while(attacksFromSquare) {
        int attackSquare = __builtin_ctzll(attacksFromSquare);
        attacksFromSquare &= attacksFromSquare - 1;

        if(((1ULL << attackSquare) & occupancyMask) != 0) {
            configIndex |= (1 << configBitPos);
        }

        configBitPos++;
    }

    return rookAttacksForOccupancy[square][configIndex];
}

uint64_t getQueenAttacksForSquareAndOccupancy(int square, uint64_t occupancy) {
    return (getBishopAttacksForSquareAndOccupancy(square, occupancy) | getRookAttacksForSquareAndOccupancy(square, occupancy));
}

uint64_t generateLegalAttacksForColor(bool white, bool checkPins, bool includeKing, bool pawnLegalMovesOnly, const Board& board) {
    uint64_t finalAttacks = 0;
    
    // rooks
    uint64_t rookBoard = board.getPieceBitBoard(white ? Piece::WHITE_ROOK : Piece::BLACK_ROOK);

    while(rookBoard) {
        int rookSquare = __builtin_ctzll(rookBoard);
        rookBoard &= rookBoard-1;

        int rank = rookSquare/8, file = rookSquare%8;

        uint64_t legalAttacksFromSquare = getRookAttacksForSquareAndOccupancy(rookSquare, board.getAllPiecesBitBoard());

        if(checkPins) {
            PinDirection direction = getPinDirection(white, rookSquare, const_cast<Board&>(board), false);

            if(direction == PinDirection::NONE) finalAttacks |= legalAttacksFromSquare;
            else if(direction == PinDirection::FILE) {
                // rook pinned in file remove rank attacks
                finalAttacks |= (~(255 << (rank * 8)) & legalAttacksFromSquare);
            }
            else if(direction == PinDirection::RANK) {
                // rook pinned in rank remove file attacks
                finalAttacks |= (~(rangeMasks[file][56+file]) & legalAttacksFromSquare);
            }
        }
        else {
            finalAttacks |= legalAttacksFromSquare;
        }
    }


    // bishops
    uint64_t bishopBoard = board.getPieceBitBoard(white ? Piece::WHITE_BISHOP : Piece::BLACK_BISHOP);

    while(bishopBoard) {
        int bishopSquare = __builtin_ctzll(bishopBoard);
        bishopBoard &= bishopBoard-1;

        int rank = bishopSquare/8, file = bishopSquare%8;

        uint64_t legalAttacksFromSquare = getBishopAttacksForSquareAndOccupancy(bishopSquare, board.getAllPiecesBitBoard());

        if(checkPins) {
            PinDirection direction = getPinDirection(white, bishopSquare, const_cast<Board&>(board), false);

            if(direction == PinDirection::NONE) finalAttacks |= legalAttacksFromSquare;
            else if(direction == PinDirection::BOTTOM_TOP_DIAG) {
                // bishop pinned bottom to top diag, remove top to bottom diag attacks
                int topLeftSquare = bishopSquare - (9 * std::min(rank, file));
                int bottomRightSquare = bishopSquare + (9 * std::min(8-rank-1, 8-file-1));

                finalAttacks |= (~(rangeMasks[topLeftSquare][bottomRightSquare]) & legalAttacksFromSquare);
            }
            else if(direction == PinDirection::TOP_BOTTOM_DIAG) {
                // bishop pinned on top to bottom diag, remove bottom to topo diag attacks
                int bottomLeftSquare = bishopSquare + (7 * std::min(8-rank-1, file));
                int topRightSquare = bishopSquare - (7 * std::min(rank, 8-file-1));

                finalAttacks |= (~(rangeMasks[bottomLeftSquare][topRightSquare]) & legalAttacksFromSquare);
            }
        }
        else {
            finalAttacks |= legalAttacksFromSquare;
        }
    }



    // knights
    uint64_t knightsBoard = board.getPieceBitBoard(white ? Piece::WHITE_KNIGHT : Piece::BLACK_KNIGHT);

    while(knightsBoard) {
        int knightSquare = __builtin_ctzll(knightsBoard);
        knightsBoard &= knightsBoard-1;

        if(!checkPins || getPinDirection(white, knightSquare, const_cast<Board&>(board), false) == PinDirection::NONE) finalAttacks |= knightAttackSquares[knightSquare];
    }



    // queens
    uint64_t queensBoard = board.getPieceBitBoard(white ? Piece::WHITE_QUEEN : Piece::BLACK_QUEEN);

    while(queensBoard) {
        int queenSquare = __builtin_ctzll(queensBoard);
        queensBoard &= queensBoard-1;

        int rank = queenSquare/8, file = queenSquare%8;

        uint64_t legalAttacksFromSquare = getQueenAttacksForSquareAndOccupancy(queenSquare, board.getAllPiecesBitBoard());

        if(checkPins) {
            PinDirection direction = getPinDirection(white, queenSquare, const_cast<Board&>(board), false);

            if(direction == PinDirection::NONE) finalAttacks |= legalAttacksFromSquare;
            else if(direction == PinDirection::BOTTOM_TOP_DIAG) {
                // bishop pinned bottom to top diag, remove top to bottom diag attacks
                int topLeftSquare = queenSquare - (9 * std::min(rank, file));
                int bottomRightSquare = queenSquare + (9 * std::min(8-rank-1, 8-file-1));

                finalAttacks |= (~(rangeMasks[topLeftSquare][bottomRightSquare]) & legalAttacksFromSquare);
            }
            else if(direction == PinDirection::TOP_BOTTOM_DIAG) {
                // bishop pinned on top to bottom diag, remove bottom to topo diag attacks
                int bottomLeftSquare = queenSquare + (7 * std::min(8-rank-1, file));
                int topRightSquare = queenSquare - (7 * std::min(rank, 8-file-1));

                finalAttacks |= (~(rangeMasks[bottomLeftSquare][topRightSquare]) & legalAttacksFromSquare);
            }
            else if(direction == PinDirection::FILE) {
                // rook pinned in file remove rank attacks
                finalAttacks |= (~(255 << (rank * 8)) & legalAttacksFromSquare);
            }
            else if(direction == PinDirection::RANK) {
                // rook pinned in rank remove file attacks
                finalAttacks |= (~(rangeMasks[file][56+file]) & legalAttacksFromSquare);
            }
        }
        else {
            finalAttacks |= legalAttacksFromSquare;
        }
    }



    // pawns
    uint64_t pawnsBoard = board.getPieceBitBoard(white ? Piece::WHITE_PAWN : Piece::BLACK_PAWN);

    while(pawnsBoard) {
        int pawnSquare = __builtin_ctzll(pawnsBoard);
        pawnsBoard &= pawnsBoard - 1;


        if(pawnLegalMovesOnly) { // caller doesn't want to mark the attack squares, but need legal moves
            int newRank = white ? pawnSquare/8 + 1 : pawnSquare/8 - 1;
            int newFile = pawnSquare % 8 - 1;

            uint64_t leftFileAttack = 0, rightFileAttack = 0, steps = 0;
            
            if(newFile >= 0 && ((1ULL << (newRank*8 + newFile)) & board.getColorBitBoard(!white)) != 0) leftFileAttack |= (1ULL << (newRank*8 + newFile));
            
            newFile += 2;

            if(newFile < 8 && ((1ULL << (newRank*8 + newFile)) & board.getColorBitBoard(!white)) != 0) rightFileAttack |= (1ULL << (newRank*8 + newFile));

            // normal move, first step
            int oneStepSquare = white ? pawnSquare + 8 : pawnSquare - 8;
            if(((1ULL << oneStepSquare) & board.getAllPiecesBitBoard()) == 0) steps |= (1ULL << oneStepSquare);

            // second step
            if((white ? pawnSquare/8 == 1 : pawnSquare/8 == 6)) {
                int twoStepSquare = white ? pawnSquare + 16 : pawnSquare - 16;
                if(((1ULL << twoStepSquare) & board.getAllPiecesBitBoard()) == 0) steps |= (1ULL << twoStepSquare);
            }

            if(checkPins) {
                PinDirection direction = getPinDirection(white, pawnSquare, const_cast<Board&>(board), false);

                if(direction == PinDirection::NONE) finalAttacks |= (leftFileAttack | rightFileAttack | steps);
                else if(direction == PinDirection::BOTTOM_TOP_DIAG) finalAttacks |= (white ?  leftFileAttack : rightFileAttack);
                else if(direction == PinDirection::TOP_BOTTOM_DIAG) finalAttacks |= (white ? rightFileAttack : leftFileAttack);
            }
            else finalAttacks |= (leftFileAttack | rightFileAttack | steps);
        }
        else { // caller just wants to mark attack squares

            int newRank = white ? pawnSquare/8 + 1 : pawnSquare/8 - 1;

            uint64_t leftFileAttack = 0, rightFileAttack = 0;

            int newFile = (pawnSquare % 8) - 1;
            
            if(newFile >= 0) leftFileAttack |= (1ULL << (newRank*8 + newFile));
            
            newFile += 2;

            if(newFile < 8) rightFileAttack |= (1ULL << (newRank*8 + newFile));

            if(checkPins) {
                PinDirection direction = getPinDirection(white, pawnSquare, const_cast<Board&>(board), false);

                if(direction == PinDirection::NONE) finalAttacks |= (leftFileAttack | rightFileAttack);
                else if(direction == PinDirection::BOTTOM_TOP_DIAG) finalAttacks |= (white ?  leftFileAttack : rightFileAttack);
                else if(direction == PinDirection::TOP_BOTTOM_DIAG) finalAttacks |= (white ? rightFileAttack : leftFileAttack);
            }
            else finalAttacks |= (leftFileAttack | rightFileAttack);
        }
    }



    // king
    if(includeKing) {
        uint64_t kingBoard = board.getPieceBitBoard(white ? Piece::WHITE_KING : Piece::BLACK_KING);

        int kingSquare = __builtin_ctzll(kingBoard);

        finalAttacks |= kingAttackSquares[kingSquare];
    }


    
    return finalAttacks;
}

bool isKingUnderCheck(bool white, const Board& board) {
    uint64_t opponentAttacks = generateLegalAttacksForColor(!white, false, true, false, board);
    int kingSquare = __builtin_ctzll(board.getPieceBitBoard(white ? Piece::WHITE_KING : Piece::BLACK_KING));

    return (((1ULL << kingSquare) & opponentAttacks) != 0);
}

bool canAnyPieceMove(bool white, const Board& board) {
    // Rooks
    uint64_t rookBoard = board.getPieceBitBoard(white ? Piece::WHITE_ROOK : Piece::BLACK_ROOK);

    while(rookBoard) {
        int rookSquare = __builtin_ctzll(rookBoard);
        rookBoard &= rookBoard-1;

        PinDirection direction = getPinDirection(white, rookSquare, const_cast<Board&>(board), false); 

        if(direction == PinDirection::FILE || direction == PinDirection::RANK) return true;
        if(direction == PinDirection::BOTTOM_TOP_DIAG || direction == PinDirection::TOP_BOTTOM_DIAG) continue;

        // if the rook can move freely, see if it is not blocked in any of the directions by it's friend pieces
        int rank = rookSquare/8, file = rookSquare%8;
        uint64_t legalAttacksFromSquare = getRookAttacksForSquareAndOccupancy(rookSquare, board.getAllPiecesBitBoard());
        uint64_t friendOccupancyMask = (legalAttacksFromSquare & board.getColorBitBoard(white));

        // check each direction, left, top, right and bottom to see if there are any friendly blockers
        // left
        if(rank*8 != rookSquare) { // if rook square is at left edge, it cannot move any further to left
            int leftMask = rangeMasks[rank*8][rookSquare];
            leftMask &= ~(1ULL << rookSquare);

            if((friendOccupancyMask & leftMask) == 0 || __builtin_clzll(friendOccupancyMask & leftMask) - rookSquare < 1) return true;
        }

        // right
        if(rank*8 + 7 != rookSquare) { // if rook square is at right edge, it cannot move any further to right
            int rightMask = rangeMasks[rank*8 + 7][rookSquare];
            rightMask &= ~(1ULL << rookSquare);

            if((friendOccupancyMask & rightMask) == 0 || __builtin_clzll(friendOccupancyMask & rightMask) - rookSquare > 1) return true;
        }

        // top
        if(file != rookSquare) { // if rook square is at top edge, it cannot move any further to top
            int topMask = rangeMasks[file][rookSquare];
            topMask &= ~(1ULL << rookSquare);

            if((friendOccupancyMask & topMask) == 0 || __builtin_clzll(friendOccupancyMask & topMask) - rookSquare < 8) return true;
        }

        // bottom
        if(56+file != rookSquare) { // if rook square is at bottom edge, it cannot move any further to bottom
            int bottomMask = rangeMasks[56 + file][rookSquare];
            bottomMask &= ~(1ULL << rookSquare);

            if((friendOccupancyMask & bottomMask) == 0 || __builtin_clzll(friendOccupancyMask & bottomMask) - rookSquare > 8) return true;
        }
    }


    // Bishops
    uint64_t bishopBoard = board.getPieceBitBoard(white ? Piece::WHITE_BISHOP : Piece::BLACK_BISHOP);

    while(bishopBoard) {
        int bishopSquare = __builtin_ctzll(bishopBoard);
        bishopBoard &= bishopBoard-1;

        PinDirection direction = getPinDirection(white, bishopSquare, const_cast<Board&>(board), false);

        if(direction == PinDirection::BOTTOM_TOP_DIAG || direction == PinDirection::TOP_BOTTOM_DIAG) return true;
        if(direction == PinDirection::RANK || direction == PinDirection::FILE) continue;

        int rank = bishopSquare/8, file = bishopSquare%8;
        uint64_t legalAttacksFromSquare = getBishopAttacksForSquareAndOccupancy(bishopSquare, board.getAllPiecesBitBoard());
        uint64_t friendOccupancyMask = (legalAttacksFromSquare & board.getColorBitBoard(white));

        // check in all directions: top left, top right, bottom left, bottom right to see if there are any friendly blockers
        // top left
        int borderSquare = bishopSquare + (-9 * std::min(rank, file));

        if(borderSquare != bishopSquare) { // if bishop is at top left edge, it cannot move any further to top left
            int topLeftMask = rangeMasks[borderSquare][bishopSquare];
            topLeftMask &= ~(1ULL << bishopSquare);

            if((friendOccupancyMask & topLeftMask) == 0 || __builtin_clzll(friendOccupancyMask & topLeftMask) - bishopSquare < 9) return true;
        }

        // top right
        borderSquare = bishopSquare + (-7 * std::min(rank, 8 - file - 1));

        if(borderSquare != bishopSquare) { // if bishop is at top left edge, it cannot move any further to top left
            int topRightMask = rangeMasks[borderSquare][bishopSquare];
            topRightMask &= ~(1ULL << bishopSquare);

            if((friendOccupancyMask & topRightMask) == 0 || __builtin_clzll(friendOccupancyMask & topRightMask) - bishopSquare < 7) return true;
        }

        // bottom right
        borderSquare = bishopSquare + (9 * std::min(8 - rank - 1, 8 - file - 1));

        if(borderSquare != bishopSquare) { // if bishop is at top left edge, it cannot move any further to top left
            int bottomRightMask = rangeMasks[borderSquare][bishopSquare];
            bottomRightMask &= ~(1ULL << bishopSquare);

            if((friendOccupancyMask & bottomRightMask) == 0 || __builtin_clzll(friendOccupancyMask & bottomRightMask) - bishopSquare > 9) return true;
        }

        // bottom left
        borderSquare = bishopSquare + (7 * std::min(8 - rank - 1, file));

        if(borderSquare != bishopSquare) { // if bishop is at top left edge, it cannot move any further to top left
            int bottomLeftMask = rangeMasks[borderSquare][bishopSquare];
            bottomLeftMask &= ~(1ULL << bishopSquare);

            if((friendOccupancyMask & bottomLeftMask) == 0 || __builtin_clzll(friendOccupancyMask & bottomLeftMask) - bishopSquare > 7) return true;
        }

    }

    // Knights
    uint64_t knightsBoard = board.getPieceBitBoard(white ? Piece::WHITE_KNIGHT : Piece::BLACK_KNIGHT);

    while(knightsBoard) {
        int knightSquare = __builtin_ctzll(knightsBoard);
        knightsBoard &= knightsBoard-1;

        uint64_t legalAttacksFromSquare = knightAttackSquares[knightSquare];
        uint64_t friendOccupancyMask = (legalAttacksFromSquare & board.getColorBitBoard(white));

        if(friendOccupancyMask < legalAttacksFromSquare) return true;
    }

    // Pawns
    uint64_t pawnsBoard = board.getPieceBitBoard(white ? Piece::WHITE_PAWN : Piece::BLACK_PAWN);

    while(pawnsBoard) {
        int pawnSquare = __builtin_ctzll(pawnsBoard);
        pawnsBoard &= pawnsBoard - 1;

        // forward movement
        int forwardSquare = white ? pawnSquare + 8 : pawnSquare - 8;
        if(((1ULL << forwardSquare) & board.getAllPiecesBitBoard()) == 0) return true;

        int rank = pawnSquare / 8, file = pawnSquare % 8;
        int leftSquare = white ? pawnSquare + 9 : pawnSquare - 9;
        // left attack
        if(file > 0 && ((1ULL << leftSquare) & board.getColorBitBoard(!white)) != 0) return true;

        int rightSquare = white ? pawnSquare + 7 : pawnSquare - 7;
        // right attack
        if(file < 7 && ((1ULL << rightSquare) & board.getColorBitBoard(!white)) != 0) return true;
    }

    return false;
}


bool isKingInSameRay(int pieceSquare, int kingSquare, int newKingSquare, Piece attackingPiece) {
    if((int)attackingPiece % 6 <= 1) return false;
    if(newKingSquare == pieceSquare) return false;

    int rank1 = pieceSquare/8, rank2 = kingSquare/8, rank3 = newKingSquare/8;
    int file1 = pieceSquare%8, file2 = kingSquare%8, file3 = newKingSquare%8;

    return (rank1 == rank2 && rank2 == rank3) || (file1 == file2 && file2 == file3) || (abs(rank1 - rank2) == abs(file1 - file2) && abs(rank1 - rank3) == abs(file1 - file3));
}


void calculateMoveResult(CheckType check, uint64_t positionHash, bool isWhiteTurn, Board& board) {
    // draw by repitition 
    if(board.positionHashToFreq[positionHash] == 3) {
        board.setGameResult(GameResult::DRAW_BY_REPITITION);
        board.drawByRepitition = true;
        return;
    }

    // opponentKingPosition
    int oppKingSquare = __builtin_ctzll(board.getPieceBitBoard(isWhiteTurn ? Piece::BLACK_KING : Piece::WHITE_KING));
    int oppKingRank = oppKingSquare / 8, oppKingFile = oppKingSquare % 8;

    int myKingSquare = board.getPieceBitBoard(isWhiteTurn ? Piece::WHITE_KING : Piece::BLACK_KING);

    // get friendly pieces position
    uint64_t friendPiecesOfOppKing = board.getColorBitBoard(!isWhiteTurn);
    friendPiecesOfOppKing &= ~(1ULL << oppKingSquare);

    // the opp king can only move to a different square
    // caluclate the attacking color piece attacks
    uint64_t myAttacks = generateLegalAttacksForColor(isWhiteTurn, false, true, false, board);


    // the opponent king is not under check
    if(check == CheckType::NO_CHECK) {
        // find if it is draw by insufficient material
        // if a pawn is present or a rook is present or a queen is present not insufficient
        if((board.getPieceBitBoard(Piece::WHITE_PAWN) | board.getPieceBitBoard(Piece::BLACK_PAWN) | board.getPieceBitBoard(Piece::WHITE_ROOK) | board.getPieceBitBoard(Piece::BLACK_ROOK) | board.getPieceBitBoard(Piece::WHITE_QUEEN) | board.getPieceBitBoard(Piece::BLACK_QUEEN)) == 0) {
            if((__builtin_popcountll(board.getPieceBitBoard(Piece::BLACK_BISHOP) | board.getPieceBitBoard(Piece::BLACK_KNIGHT)) <= 1 && __builtin_popcountll(board.getPieceBitBoard(Piece::WHITE_BISHOP) | board.getPieceBitBoard(Piece::WHITE_KNIGHT)) == 0) || 
                __builtin_popcountll(board.getPieceBitBoard(Piece::BLACK_BISHOP) | board.getPieceBitBoard(Piece::BLACK_KNIGHT)) == 0 && __builtin_popcountll(board.getPieceBitBoard(Piece::WHITE_BISHOP) | board.getPieceBitBoard(Piece::WHITE_KNIGHT)) <= 1) {
                board.setGameResult(GameResult::DRAW_BY_INSUFFICIENT_MATERIAL);
                board.drawByInsufficientMaterial = true;
                return;
            }

            uint64_t blackBishops = board.getPieceBitBoard(Piece::BLACK_BISHOP);
            uint64_t whiteBishops = board.getPieceBitBoard(Piece::WHITE_BISHOP);

            if(__builtin_popcountll(blackBishops) == 1 && __builtin_popcountll(whiteBishops) == 1) {
                int sq1 = __builtin_ctzll(blackBishops), sq2 = __builtin_ctzll(whiteBishops);
                int rank1 = sq1/8, file1 = sq1%8, rank2 = sq2/8, file2 = sq2%8;

                int res1 = abs((rank2 % 2 + file2 % 2) - (rank1 % 2 + file1 % 2));
                
                // if both bishops are of same color (if rank1 is even and file1 is even and rank2 and file2 are both odd, then the two squares are of same color || (if rank1 is odd and file 1 is even and square 2 has same or square2 has opposite parity) or viceversa) )
                if(res1 == 2 || res1 == 0) {
                    board.setGameResult(GameResult::DRAW_BY_INSUFFICIENT_MATERIAL);
                    board.drawByInsufficientMaterial = true;
                    return;
                }
            }
            
        }

        // find if it is stalemate
        // iterate all possible king squares
        for(auto & offSets : kingMoveOffsets) {
            int8_t newRank = oppKingRank + offSets[0], newFile = oppKingFile + offSets[1];

            if(newRank >= 0 && newRank < 8 && newFile >= 0 && newFile < 8) {
                int newSquare = newRank*8 + newFile;

                if((friendPiecesOfOppKing & (1ULL << newSquare)) == 0 && ((1ULL << newSquare) & myAttacks) == 0) return;
            }
        }

        // if opponent queen is present, it is not a stalemate
        if((isWhiteTurn ? board.getPieceBitBoard(Piece::BLACK_QUEEN) : board.getPieceBitBoard(Piece::WHITE_QUEEN))) return;

        if(!canAnyPieceMove(!isWhiteTurn, board)) {
            board.stalemate = true;
            board.setGameResult(GameResult::STALEMATE);
            return;    
        }

        return;
    } 


    Piece directCheckingPiece = (check == CheckType::DIRECT_CHECK || check == CheckType::DOUBLE_CHECK ? board.getPieceOnBoard(board.directCheckSquare) : Piece::EMPTY);
    Piece discoveryCheckingPiece = (check == CheckType::DISCOVERY_CHECK || check == CheckType::DOUBLE_CHECK ? board.getPieceOnBoard(board.discoveryCheckSquare) : Piece::EMPTY);

    // opponent king is under check
    // first see if he can escape
    for(auto & offSets : kingMoveOffsets) {
        int8_t newRank = oppKingRank + offSets[0], newFile = oppKingFile + offSets[1];

        if(newRank >= 0 && newRank < 8 && newFile >= 0 && newFile < 8) {
            int newSquare = newRank*8 + newFile;

            if((friendPiecesOfOppKing & (1ULL << newSquare)) == 0 && ((1ULL << newSquare) & myAttacks) == 0) {
                // if direct check is there and the piece is not a pawn and not knight, see if the new king square is still in the check direction
                if((check == CheckType::DIRECT_CHECK || check == CheckType::DOUBLE_CHECK) && !isKingInSameRay(board.directCheckSquare, oppKingSquare, newSquare, directCheckingPiece)) return;
                // same thing for discovery check
                if((check == CheckType::DISCOVERY_CHECK || check == CheckType::DOUBLE_CHECK) && !isKingInSameRay(board.directCheckSquare, oppKingSquare, newSquare, directCheckingPiece)) return;
            }
        }
    }

    // he can't escape
    if(check == CheckType::DOUBLE_CHECK) {

        if(isWhiteTurn) board.blackKingCheckmated = true;
        board.setGameResult(isWhiteTurn ? GameResult::WHITE_WON_BY_CHECKMATE : GameResult::BLACK_WON_BY_CHECKMATE);
        return;
    }


    Piece checkingPiece = (directCheckingPiece == Piece::EMPTY ? discoveryCheckingPiece : directCheckingPiece);
    int checkingPieceSquare = (directCheckingPiece == Piece::EMPTY ? board.discoveryCheckSquare : board.directCheckSquare);

    uint64_t defenderLegalMoves = generateLegalAttacksForColor(!isWhiteTurn, true, false, true, board);

    // if checking piece is a knight, killing it is the only way to prevent checkmate
    if((int)checkingPiece % 6 == 1) {
        if(((1ULL << checkingPieceSquare) & defenderLegalMoves) == 0) {
            if(isWhiteTurn) board.blackKingCheckmated = true;
            board.setGameResult(isWhiteTurn ? GameResult::WHITE_WON_BY_CHECKMATE : GameResult::BLACK_WON_BY_CHECKMATE);
        }
        return;
    }

    // see if the opponent pieces can defend their king
    uint64_t kingToCheckingPieceMask = rangeMasks[oppKingSquare][checkingPieceSquare];
    kingToCheckingPieceMask &= ~(1ULL << oppKingSquare);


    if((defenderLegalMoves & kingToCheckingPieceMask) == 0) {
        if(isWhiteTurn) board.blackKingCheckmated = true;
        board.setGameResult(isWhiteTurn ? GameResult::WHITE_WON_BY_CHECKMATE : GameResult::BLACK_WON_BY_CHECKMATE);
        return;
    }
}


bool doesColorHaveInsufficientMaterial(bool white, const Board& board) {
    // if a pawn is present or a rook is present or a queen is present not insufficient
    if((board.getPieceBitBoard(white ? Piece::WHITE_PAWN : Piece::BLACK_PAWN) | board.getPieceBitBoard(white ? Piece::WHITE_ROOK : Piece::BLACK_ROOK) | board.getPieceBitBoard(white ? Piece::WHITE_QUEEN : Piece::BLACK_QUEEN)) == 0) {
        if(__builtin_popcountll(board.getPieceBitBoard(white ? Piece::WHITE_BISHOP : Piece::BLACK_BISHOP) | board.getPieceBitBoard(white ? Piece::WHITE_KNIGHT : Piece::BLACK_KNIGHT)) <= 1) return true;        
    }

    return false;
}

};