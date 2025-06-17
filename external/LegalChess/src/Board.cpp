#include "Board.h"
#include "Helper.h"

namespace LC {

const char* const gameResultToString[7] = {"Game_In_Progress", "White_Won_By_Checkmate", "Black_Won_By_Checkmate", "Stalemate", "Draw_By_Repitition", "Draw_By_Insufficient_Material", "Draw_By_50_Half_Moves"};
char const pieceToChar[13] = {'P', 'N', 'B', 'R', 'Q', 'K', 'p', 'n', 'b', 'r', 'q', 'k', '.'};

class MoveManagerStore;

Board::Board() {
    initBoard();
}

void Board::initBoard() {
    piecesArray[0] = 255;
    piecesArray[0] <<= 8; // Pawns

    piecesArray[1] = 66; // Knights
    piecesArray[2] = 36; // Bishops
    piecesArray[3] = 129; // Rooks
    piecesArray[4] = 16; // Queens
    piecesArray[5] = 8; // King

    
    piecesArray[6] = (piecesArray[0] << 40); // Pawns

    piecesArray[7] = (piecesArray[1] << 56); // Knights
    piecesArray[8] = (piecesArray[2] << 56); // Bishops
    piecesArray[9] = (piecesArray[3] << 56); // Rooks
    piecesArray[10] = (piecesArray[4] << 56); // Queens
    piecesArray[11] = (piecesArray[5] << 56); // King

    allPiecesBoard = allWhitePiecesBoard = allBlackPiecesBoard = 0;

    for(int i = 0; i<12; i++) {
        uint64_t pieces = piecesArray[i];
        allPiecesBoard |= pieces;

        if(i < 6) allWhitePiecesBoard |= pieces;
        else allBlackPiecesBoard |= pieces;
    }

    // WhitePieces
    // Pawn Row
    for(auto &piece : grid[1]) piece = Piece::WHITE_PAWN;

    // King Row
    grid[0][0] = grid[0][7] = Piece::WHITE_ROOK;
    grid[0][1] = grid[0][6] = Piece::WHITE_KNIGHT;
    grid[0][2] = grid[0][5] = Piece::WHITE_BISHOP;
    grid[0][3] = Piece::WHITE_KING;
    grid[0][4] = Piece::WHITE_QUEEN;

    // Black Pieces
    // King Row
    grid[7][0] = grid[7][7] = Piece::BLACK_ROOK;
    grid[7][1] = grid[7][6] = Piece::BLACK_KNIGHT;
    grid[7][2] = grid[7][5] = Piece::BLACK_BISHOP;
    grid[7][3] = Piece::BLACK_KING;
    grid[7][4] = Piece::BLACK_QUEEN;

    // Pawn Row
    for(auto &piece : grid[6]) piece = Piece::BLACK_PAWN;


    // fill empty squares
    for(int rank = 2; rank < 6; rank++) {
        for(auto &piece : grid[rank]) piece = Piece::EMPTY;
    }

    isWhiteTurn = true;
    gameOver = whiteKingCheckmated = blackKingCheckmated = stalemate = drawBy50HalfMoves = drawByInsufficientMaterial = drawByRepitition = false;
    canWhiteKingShortCastle = canBlackKingShortCastle = canWhiteKingLongCastle = canBlackKingLongCastle = true;

    enpassantSquare = discoveryCheckSquare = directCheckSquare = 64;

    halfMovesCount = movesCount = 0; // they treat each player's turn as different moves

    m_pMoveManagerStore = MoveManagerStore::getMoveManagerStore();
    m_pZobrist = Zobrist::getInstance();

    gameResult = GameResult::IN_PROGRESS;
}


void Board::move(const Move& move) {
    Piece movingPiece = grid[move.fromRow][move.fromCol];

    if(movingPiece == Piece::EMPTY) {
        // throw error
        throw EmptySquareException("There is no piece on the moving square mentioned. Move number: " + std::to_string(movesCount + 1) + ". Move: " + std::string(move.uciMove));
    }

    // check turn, 0 to 5 are white pieces
    if(isWhiteTurn != ((int)movingPiece/6 == 0)) {
        // throw error
        throw PlayerTurnException("It is " + std::string(isWhiteTurn ? "white's " : "black's ") + " turn to move. Move number: " + std::to_string(movesCount + 1) + ". Move: " + std::string(move.uciMove));
    }

    CheckType check = m_pMoveManagerStore->getPieceMoveManager(movingPiece)->handleMove(isWhiteTurn, move, *this);

    movesCount++;
    if((int)movingPiece % 6 == 0 || grid[move.toRow][move.toCol] != Piece::EMPTY) halfMovesCount = 0;
    else halfMovesCount++;

    // update the move in grid
    grid[move.toRow][move.toCol] = movingPiece;
    grid[move.fromRow][move.fromCol] = Piece::EMPTY;

    // update enpassant square if the moved piece is not a pawn
    if(movingPiece != Piece::WHITE_PAWN && movingPiece != Piece::BLACK_PAWN) enpassantSquare = 64;

    int castlingRights = 0;
    if(canWhiteKingShortCastle) castlingRights |= (1 << 0);
    if(canWhiteKingLongCastle) castlingRights |= (1 << 1);
    if(canBlackKingShortCastle) castlingRights |= (1 << 2);
    if(canBlackKingLongCastle) castlingRights |= (1 << 3);

    uint64_t positionHash = m_pZobrist->computeHash(grid, !isWhiteTurn, castlingRights, enpassantSquare % 8);
    positionHashToFreq[positionHash]++;

    calculateMoveResult(check, positionHash, isWhiteTurn, *this);

    // check for 50 move rule
    if(halfMovesCount == 100 && gameResult == GameResult::IN_PROGRESS) {
        drawBy50HalfMoves = true;
        setGameResult(GameResult::DRAW_BY_50_HALF_MOVES);
    }

    isWhiteTurn = !isWhiteTurn;    
}


void Board::promote(char choosenPiece, const Move& move) {
    Piece movingPiece = grid[move.fromRow][move.fromCol];

    if(movingPiece == Piece::EMPTY || (movingPiece != Piece::WHITE_PAWN && movingPiece != Piece::BLACK_PAWN)) {
        // throw error
        throw EmptySquareException("There is no pawn on the moving square mentioned for promotion. Move number: " + std::to_string(movesCount + 1) + ". Move: " + std::string(move.uciMove));
    }

    // check turn, 0 to 5 are white pieces
    if(isWhiteTurn != ((int)movingPiece/6 == 0)) {
        // throw error
        throw PlayerTurnException("It is " + std::string(isWhiteTurn ? "white's " : "black's ") + " turn to move. Move number: " + std::to_string(movesCount + 1) + ". Move: " + std::string(move.uciMove));
    }

    Piece newPiece;

    if(choosenPiece == 'q') newPiece = isWhiteTurn ? Piece::WHITE_QUEEN : Piece::BLACK_QUEEN;
    else if(choosenPiece == 'r') newPiece = isWhiteTurn ? Piece::WHITE_ROOK : Piece::BLACK_ROOK;
    else if(choosenPiece == 'b') newPiece = isWhiteTurn ? Piece::WHITE_BISHOP : Piece::BLACK_BISHOP;
    else if(choosenPiece == 'n') newPiece = isWhiteTurn ? Piece::WHITE_KNIGHT : Piece::BLACK_KNIGHT;
    else throw InvalidMoveException("The provied piece to promote is invalid: " + std::string(1, choosenPiece) + ". Move number: " + std::to_string(movesCount + 1) + ". Move: " + std::string(move.uciMove));

    CheckType check = std::static_pointer_cast<PawnMoveManager>(m_pMoveManagerStore->getPieceMoveManager(movingPiece))->handlePromotion(newPiece, isWhiteTurn, move, *this);

    // update the move in grid
    grid[move.toRow][move.toCol] = newPiece;
    grid[move.fromRow][move.fromCol] = Piece::EMPTY;

    // update enpassant square if the moved piece is not a pawn
    if(movingPiece != Piece::WHITE_PAWN && movingPiece != Piece::BLACK_PAWN) enpassantSquare = 64;

    int castlingRights = 0;
    if(canWhiteKingShortCastle) castlingRights |= (1 << 0);
    if(canWhiteKingLongCastle) castlingRights |= (1 << 1);
    if(canBlackKingShortCastle) castlingRights |= (1 << 2);
    if(canBlackKingLongCastle) castlingRights |= (1 << 3);

    uint64_t positionHash = m_pZobrist->computeHash(grid, !isWhiteTurn, castlingRights, enpassantSquare % 8);
    positionHashToFreq[positionHash]++;

    calculateMoveResult(check, positionHash, isWhiteTurn, *this);

    isWhiteTurn = !isWhiteTurn;
    movesCount++;
    halfMovesCount = 0;
}

bool Board::doesColorHaveInsufficientMaterial(bool white) {
    return LC::doesColorHaveInsufficientMaterial(white, *this);
}

bool Board::isKingUnderCheck(bool white) const {
    return LC::isKingUnderCheck(white, *this);
}


std::string Board::getFENString() const {
    std::string fenString = "";

    for(int i = 7; i >= 0; i--) {
        int emptyCount = 0;
        for(int j = 7; j >= 0; j--) {
            if(grid[i][j] == Piece::EMPTY) emptyCount++;
            else {
                if(emptyCount) fenString.push_back('0' + emptyCount);
                fenString.push_back(pieceToChar[(int)grid[i][j]]);
            }
        }

        if(emptyCount) fenString.push_back('0' + emptyCount);
        if(i!= 0) fenString.push_back('/');
    }

    fenString.push_back(' ');
    fenString.push_back(isWhiteTurn ? 'w' : 'b');
    
    fenString.push_back(' ');
    // Castling Rights
    std::string castling = "";
    if (canWhiteKingShortCastle) castling += 'K';
    if (canWhiteKingLongCastle)  castling += 'Q';
    if (canBlackKingShortCastle) castling += 'k';
    if (canBlackKingLongCastle)  castling += 'q';
    fenString += castling.empty() ? "-" : castling;

    fenString.push_back(' ');

    // enpassant square
    if (enpassantSquare != 64) {
        char file = 'h' - (enpassantSquare % 8);
        char rank = '1' + (enpassantSquare / 8);
        fenString += file;
        fenString += rank;
    } else {
        fenString += "-";
    }

    fenString.push_back(' ');
    fenString += std::to_string(halfMovesCount);

    fenString.push_back(' ');
    fenString += std::to_string(movesCount/2 + 1);

    return fenString;
}


};