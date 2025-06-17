#ifndef __BOARD_H__
#define __BOARD_H__

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <memory>

#include "MoveManager.h"
#include "Zobrist.h"
#include "Helper.h"

namespace LC {

class MoveManager;
class MoveManagerStore;
class Zobrist;

class InvalidMoveException : public std::runtime_error {
public:
    InvalidMoveException(std::string msg) : std::runtime_error(msg) {}
};

class PlayerTurnException : public std::runtime_error {
public:
    PlayerTurnException(std::string msg) : std::runtime_error(msg) {}
};

class EmptySquareException : public std::runtime_error {
public:
    EmptySquareException(std::string msg) : std::runtime_error(msg) {}
};

class InvalidMovePatternException : public std::runtime_error {
public:
    InvalidMovePatternException(std::string msg) : std::runtime_error(msg) {}
};

class BlockedMoveException : public std::runtime_error {
public:
    BlockedMoveException(std::string msg) : std::runtime_error(msg) {}
};

class KingUnderCheckException : public std::runtime_error {
public:
    KingUnderCheckException(std::string msg) : std::runtime_error(msg) {}
};

class GameOverException : public std::runtime_error {
public:
    GameOverException(std::string msg) : std::runtime_error(msg) {}
};

class KingCastleException : public std::runtime_error {
public:
    KingCastleException(std::string msg) : std::runtime_error(msg) {}
};

struct Move {
    int fromRow;
    int fromCol;
    int toRow;
    int toCol;
    int fromSquare;
    int toSquare;
    std::string_view uciMove;

    Move() {
        
    }

    Move(int fr, int fc, int tr, int tc, int fs, int ts, std::string_view um) {
        fromRow = fr;
        fromCol = fc;
        toRow = tr;
        toCol = tc;
        fromSquare = fs;
        toSquare = ts;
        uciMove = um;
    }
};

enum class Piece {
    WHITE_PAWN, 
    WHITE_KNIGHT,
    WHITE_BISHOP,
    WHITE_ROOK,
    WHITE_QUEEN,
    WHITE_KING,
    BLACK_PAWN,
    BLACK_KNIGHT,
    BLACK_BISHOP,
    BLACK_ROOK,
    BLACK_QUEEN,
    BLACK_KING,
    EMPTY
};

enum class CheckType {
    NO_CHECK,
    DIRECT_CHECK,
    DISCOVERY_CHECK,
    DOUBLE_CHECK
};

enum class GameResult {
    IN_PROGRESS,
    WHITE_WON_BY_CHECKMATE,
    BLACK_WON_BY_CHECKMATE,
    STALEMATE,
    DRAW_BY_REPITITION,
    DRAW_BY_INSUFFICIENT_MATERIAL,
    DRAW_BY_50_HALF_MOVES
};

extern const char* const gameResultToString[7];
extern char const pieceToChar[13];

class Board {
public:
    Board();
    ~Board() = default;

    void move(const Move&);
    void promote(char choosenPiece, const Move&);
    
    
    inline void updatePieceMoveOnBoard(Piece piece, int fromSquare, int toSquare) {
        int pieceIndex = (int)piece;

        uint64_t& colorBoard = pieceIndex < 6 ? allWhitePiecesBoard : allBlackPiecesBoard;
        uint64_t& pieceBoard = piecesArray[pieceIndex];

        // remove piece on all boards from fromSquare
        colorBoard &= ~(1ULL << fromSquare);
        pieceBoard &= ~(1ULL << fromSquare);
        allPiecesBoard &= ~(1ULL << fromSquare);

        // add piece on all boards to toSquare
        colorBoard |= (1ULL << toSquare);
        pieceBoard |= (1ULL << toSquare);
        allPiecesBoard |= (1ULL << toSquare);   
    }

    inline void updatePieceCountOnBoard(Piece piece, int square, bool inc) {
        int pieceIndex = (int)piece;

        uint64_t& colorBoard = pieceIndex < 6 ? allWhitePiecesBoard : allBlackPiecesBoard;
        uint64_t& pieceBoard = piecesArray[pieceIndex];

        if(inc) {
            // add piece on all boards to square
            colorBoard |= (1ULL << square);
            pieceBoard |= (1ULL << square);
            allPiecesBoard |= (1ULL << square);  
        }
        else {
            // remove piece on all boards from square
            colorBoard &= ~(1ULL << square);
            pieceBoard &= ~(1ULL << square);
            allPiecesBoard &= ~(1ULL << square);
        }
    }
        
    inline uint64_t getPieceBitBoard(Piece piece) const {
        return piecesArray[(int)piece];
    }

    inline uint64_t getColorBitBoard(bool white) const {
        return white ? allWhitePiecesBoard : allBlackPiecesBoard;
    }

    inline uint64_t getAllPiecesBitBoard() const {
        return allPiecesBoard;
    }

    inline Piece getPieceOnBoard(int square) const {
        return grid[square/8][square%8];
    }

    inline void updateDiscoveryCheckSquare(int square) {
        discoveryCheckSquare = square;
    }

    inline void updateDirectCheckSquare(int square) {
        directCheckSquare = square;
    }

    inline bool isGameOver() const {
        return gameResult != GameResult::IN_PROGRESS;
    }

    inline bool isCheckmate(bool white) const {
        return white ? whiteKingCheckmated : blackKingCheckmated;
    }

    inline bool isStalemate() const {
        return stalemate;
    }

    inline bool isDrawByRepitition() const {
        return drawByRepitition;
    }

    inline bool isDrawBy50HalfMoves() const {
        return drawBy50HalfMoves;
    }

    inline bool isDrawyByInsufficientMaterial() const {
        return drawByInsufficientMaterial;
    }

    inline std::vector<std::vector<char>> getBoard() const {
        std::vector<std::vector<char>> board(8, std::vector<char>(8));

        for(int i = 0; i<8; i++) {
            for(int j = 0; j<8; j++) {
                board[i][j] = pieceToChar[(int)grid[i][j]];
            }
        }

        return board;
    }

    inline std::string getMoveHistory() const {
        return moveHistory;
    }

    inline void addMoveToHistory(std::string move) {
        if(moveHistory.length() != 0) moveHistory.push_back(' ');
        moveHistory += move;
    }

    inline void setPieceOnBoard(Piece piece, int square) {
        grid[square/8][square%8] = piece;
    }

    inline int getMoveNumber() {
        return movesCount;
    }

    inline GameResult getGameResult() const {
        return gameResult;
    }

    inline void setGameResult(GameResult result) {
        gameResult = result;
    }

    bool doesColorHaveInsufficientMaterial(bool white);

    bool isKingUnderCheck(bool white) const;

    std::string getFENString() const;


    // member variables
    std::unordered_map<uint64_t, uint16_t> positionHashToFreq;
    bool isWhiteTurn;
    bool canWhiteKingShortCastle, canWhiteKingLongCastle, canBlackKingShortCastle, canBlackKingLongCastle;
    bool gameOver, whiteKingCheckmated, blackKingCheckmated, stalemate, drawByRepitition, drawBy50HalfMoves, drawByInsufficientMaterial;

    uint16_t enpassantSquare, discoveryCheckSquare, directCheckSquare;
    uint16_t halfMovesCount, movesCount; // they treat each player's turn as different moves
    
    GameResult gameResult;

private:
    void initBoard();

    uint64_t piecesArray[12];
    uint64_t allWhitePiecesBoard, allBlackPiecesBoard, allPiecesBoard;

    Piece grid[8][8];
    std::string moveHistory;

    std::shared_ptr<const MoveManagerStore> m_pMoveManagerStore;
    std::shared_ptr<const Zobrist> m_pZobrist;
};


};

#endif
