#include <memory>
#include "MoveManager.h"
#include "Helper.h"

namespace LC {

CheckType PawnMoveManager::handlePromotion(Piece newPiece, bool isPieceWhite, const Move& move, Board& board) {
    // check move pattern
    if((isPieceWhite ? move.fromRow != 6 : move.fromRow != 1) || (isPieceWhite ? move.toRow != 7 : move.toRow != 0) || abs(move.fromCol - move.toCol) > 1) {
        // throw error
        throw InvalidMovePatternException("Invalid move pattern for a pawn. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove)); 
    }


    Piece capturedPiece = board.getPieceOnBoard(move.toSquare);
    Piece movedPiece = board.getPieceOnBoard(move.fromSquare);

    // if capture
    if(move.fromCol != move.toCol && capturedPiece == Piece::EMPTY) {
        // throw error
        throw InvalidMovePatternException("Invalid move pattern for a pawn. New square doesn't have an opponent piece to capture. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove));
    }

    if(capturedPiece != Piece::EMPTY && isPieceWhite == (int)capturedPiece < 6) {
        // throw error
        throw BlockedMoveException("The pawn is blocked. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove));
    }

    // if capture, update the captured piece on board (don't have check for the validity of toSquare as it is taken care in board.move())
    if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, false);

    // remove pawn
    board.updatePieceCountOnBoard(movedPiece, move.fromSquare, false);

    // add new piece
    board.updatePieceCountOnBoard(newPiece, move.toSquare, true);


    if(isKingUnderCheck(isPieceWhite, board)) {  // is my king under check after my move is made
        // undo the move
        board.updatePieceCountOnBoard(newPiece, move.toSquare, false);

        board.updatePieceCountOnBoard(movedPiece, move.fromSquare, true);

        if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, true);

        // throw error
        throw KingUnderCheckException("Cannot move piece. It is either pinned or the king is under check. Move number: " + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
    }

    uint64_t pieceAttacks = 0;

    if(newPiece == Piece::WHITE_QUEEN || newPiece == Piece::BLACK_QUEEN) pieceAttacks = getQueenAttacksForSquareAndOccupancy(move.toSquare, board.getAllPiecesBitBoard());
    else if(newPiece == Piece::WHITE_KNIGHT || newPiece == Piece::BLACK_KNIGHT) pieceAttacks = knightAttackSquares[move.toSquare];
    else if(newPiece == Piece::WHITE_BISHOP || newPiece == Piece::BLACK_BISHOP) pieceAttacks = getBishopAttacksForSquareAndOccupancy(move.toSquare, board.getAllPiecesBitBoard());
    else if(newPiece == Piece::WHITE_ROOK || newPiece == Piece::BLACK_ROOK) pieceAttacks = getRookAttacksForSquareAndOccupancy(move.toSquare, board.getAllPiecesBitBoard());


    CheckType check = CheckType::NO_CHECK;

    // Return the check type the move results in
    if((pieceAttacks & (1ULL << __builtin_ctzll(board.getPieceBitBoard(isPieceWhite ? Piece::BLACK_KING : Piece::WHITE_KING))))) { // if the new piece directly checks the opponent king
        // update the direct check square
        board.directCheckSquare = move.toSquare;

        check = CheckType::DIRECT_CHECK;
    }

    if(getPinDirection(!isPieceWhite, move.fromSquare, board, true) != PinDirection::NONE) { // if the move results in discovery check
        check = check == CheckType::NO_CHECK ? CheckType::DISCOVERY_CHECK : CheckType::DOUBLE_CHECK;
    }

    return check;
}

CheckType PawnMoveManager::handleMove(bool isPieceWhite, const Move& move, Board& board) {
    // check move pattern
    if((isPieceWhite ? move.fromRow - move.toRow > 0 : move.fromRow - move.toRow < 0) || (abs(move.fromRow - move.toRow) == 2 && (move.fromRow != 6 && move.fromRow != 1)) || abs(move.fromRow - move.toRow) > 2) {
        // throw error
        throw InvalidMovePatternException("Invalid move pattern for a pawn. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove)); 
    }

    Piece capturedPiece = board.getPieceOnBoard(move.toSquare);
    Piece movedPiece = board.getPieceOnBoard(move.fromSquare);

    // check blocks
    uint64_t pathMask = rangeMasks[move.fromSquare][move.toSquare];
    pathMask &= ~(1ULL << move.fromSquare);
    pathMask &= ~(1ULL << move.toSquare);

    // if capture
    if(move.fromCol != move.toCol && (capturedPiece == Piece::EMPTY && move.toSquare != board.enpassantSquare)) {
        // throw error
        throw InvalidMovePatternException("Invalid move pattern for a pawn. New square doesn't have an opponent piece to capture. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove));
    }

    if((pathMask & board.getAllPiecesBitBoard()) != 0 || (capturedPiece != Piece::EMPTY && isPieceWhite == (int)capturedPiece < 6)) {
        // throw error
        throw BlockedMoveException("The pawn is blocked. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove));
    }

    // if capture, update the captured piece on board (don't have check for the validity of toSquare as it is taken care in board.move())
    if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, false);
    if(move.toSquare == board.enpassantSquare) board.updatePieceCountOnBoard(isPieceWhite ? Piece::BLACK_PAWN : Piece::WHITE_PAWN, move.fromRow*8 + move.toCol, false);

    // update the move on board
    board.updatePieceMoveOnBoard(movedPiece, move.fromSquare, move.toSquare);


    if(isKingUnderCheck(isPieceWhite, board)) { // is my king under check after my move is made
        // undo the move
        if(move.toSquare == board.enpassantSquare) board.updatePieceCountOnBoard(isPieceWhite ? Piece::BLACK_PAWN : Piece::WHITE_PAWN, move.fromRow*8 + move.toCol, true);

        board.updatePieceMoveOnBoard(movedPiece, move.toSquare, move.fromSquare);

        if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, true);

        // throw error
        throw KingUnderCheckException("Cannot move piece. It is either pinned or the king is under check. Move number: " + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
    }

    // update enpassant square
    if(abs(move.fromRow - move.toRow) == 2) {
        board.enpassantSquare = isPieceWhite ? move.fromSquare + 8 : move.fromSquare - 8;
    }
    else if(move.toSquare == board.enpassantSquare) {
        // update grid
        board.setPieceOnBoard(Piece::EMPTY, move.fromSquare*8 + move.toCol);
    }

    uint64_t pieceAttacks = 0; 

    int newRank = isPieceWhite ? move.fromRow + 1 : move.fromRow - 1;
    if(move.fromCol > 0) pieceAttacks |= (1ULL << (newRank*8 + move.fromCol - 1));
    if(move.fromCol < 7) pieceAttacks |= (1ULL << (newRank*8 + move.fromCol + 1));

    CheckType check = CheckType::NO_CHECK;

    // Return the check type the move results in
    if((pieceAttacks & (1ULL << __builtin_ctzll(board.getPieceBitBoard(isPieceWhite ? Piece::BLACK_KING : Piece::WHITE_KING))))) { // if the new piece directly checks the opponent king
        // update the direct check square
        board.directCheckSquare = move.toSquare;

        check = CheckType::DIRECT_CHECK;
    }

    if(getPinDirection(!isPieceWhite, move.fromSquare, board, true) != PinDirection::NONE) { // if the move results in discovery check
        check = check == CheckType::NO_CHECK ? CheckType::DISCOVERY_CHECK : CheckType::DOUBLE_CHECK;
    }

    return check;
}

CheckType KnightMoveManager::handleMove(bool isPieceWhite, const Move& move, Board& board) {
    // check move pattern
    if((abs(move.fromRow - move.toRow) == 1 && abs(move.fromCol - move.toCol) == 2) || (abs(move.fromRow - move.toRow) == 2 && abs(move.fromCol - move.toCol) == 1)) {
        Piece capturedPiece = board.getPieceOnBoard(move.toSquare);
        Piece movedPiece = board.getPieceOnBoard(move.fromSquare);

        if(capturedPiece != Piece::EMPTY && isPieceWhite == (int)capturedPiece < 6) {
            // throw error
            throw BlockedMoveException("The knight is moved to square which is occupied by same color piece. Move number: " + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
        }

        // if capture, update the captured piece on board (don't have check for the validity of toSquare as it is taken care in board.move())
        if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, false);

        // update the move on board
        board.updatePieceMoveOnBoard(movedPiece, move.fromSquare, move.toSquare);


        if(isKingUnderCheck(isPieceWhite, board)) { // is my king under check after my move is made
            // undo the move
            board.updatePieceMoveOnBoard(movedPiece, move.toSquare, move.fromSquare);

            if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, true);

            // throw error
            throw KingUnderCheckException("Cannot move piece. It is either pinned or the king is under check. Move number: " + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
        }

        uint64_t pieceAttacks = knightAttackSquares[move.toSquare];

        CheckType check = CheckType::NO_CHECK;

        // Return the check type the move results in
        if((pieceAttacks & (1ULL << __builtin_ctzll(board.getPieceBitBoard(isPieceWhite ? Piece::BLACK_KING : Piece::WHITE_KING))))) { // if the new piece directly checks the opponent king
            // update the direct check square
            board.directCheckSquare = move.toSquare;

            check = CheckType::DIRECT_CHECK;
        }

        if(getPinDirection(!isPieceWhite, move.fromSquare, board, true) != PinDirection::NONE) { // if the move results in discovery check
            check = check == CheckType::NO_CHECK ? CheckType::DISCOVERY_CHECK : CheckType::DOUBLE_CHECK;
        }

        return check;
    }

    // throw error
    throw InvalidMovePatternException("Invalid move pattern for a knight. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove)); 
}

CheckType BishopMoveManager::handleMove(bool isPieceWhite, const Move& move, Board& board) {
    // check move pattern
    if(abs(move.fromRow - move.toRow) != abs(move.fromCol - move.toCol)) {
        // throw error
        throw InvalidMovePatternException("Invalid move pattern for a bishop. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove)); 
    }

    Piece capturedPiece = board.getPieceOnBoard(move.toSquare);
    Piece movedPiece = board.getPieceOnBoard(move.fromSquare);

    // check blocks
    uint64_t pathMask = rangeMasks[move.fromSquare][move.toSquare];
    pathMask &= ~(1ULL << move.fromSquare);
    pathMask &= ~(1ULL << move.toSquare);

    if((pathMask & board.getAllPiecesBitBoard()) != 0 || (capturedPiece != Piece::EMPTY && isPieceWhite == ((int)capturedPiece) < 6)) {
        // throw error
        throw BlockedMoveException("The bishop is blocked. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove));
    }


    // if capture, update the captured piece on board (don't have check for the validity of toSquare as it is taken care in board.move())
    if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, false);

    // update the move on board
    board.updatePieceMoveOnBoard(movedPiece, move.fromSquare, move.toSquare);


    if(isKingUnderCheck(isPieceWhite, board)) { // is my king under check after my move is made
        // undo the move
        board.updatePieceMoveOnBoard(movedPiece, move.toSquare, move.fromSquare);

        if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, true);

        // throw error
        throw KingUnderCheckException("Cannot move piece. It is either pinned or the king is under check. Move number: " + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
    }

    uint64_t pieceAttacks = getBishopAttacksForSquareAndOccupancy(move.toSquare, board.getAllPiecesBitBoard());

    CheckType check = CheckType::NO_CHECK;

    // Return the check type the move results in
    if((pieceAttacks & (1ULL << __builtin_ctzll(board.getPieceBitBoard(isPieceWhite ? Piece::BLACK_KING : Piece::WHITE_KING))))) { // if the new piece directly checks the opponent king
        // update the direct check square
        board.directCheckSquare = move.toSquare;

        check = CheckType::DIRECT_CHECK;
    }

    if(getPinDirection(!isPieceWhite, move.fromSquare, board, true) != PinDirection::NONE) { // if the move results in discovery check
        check = check == CheckType::NO_CHECK ? CheckType::DISCOVERY_CHECK : CheckType::DOUBLE_CHECK;
    }

    return check;
}

CheckType RookMoveManager::handleMove(bool isPieceWhite, const Move& move, Board& board) {
    if(move.fromRow != move.toRow && move.fromCol != move.toCol) {
        // throw error
        throw InvalidMovePatternException("Invalid move pattern for a rook. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove)); 
    }

    Piece capturedPiece = board.getPieceOnBoard(move.toSquare);
    Piece movedPiece = board.getPieceOnBoard(move.fromSquare);

    // check blocks
    uint64_t pathMask = rangeMasks[move.fromSquare][move.toSquare];
    pathMask &= ~(1ULL << move.fromSquare);
    pathMask &= ~(1ULL << move.toSquare);

    if((pathMask & board.getAllPiecesBitBoard()) != 0 || (capturedPiece != Piece::EMPTY && isPieceWhite == (int)capturedPiece < 6)) {
        // throw error
        throw BlockedMoveException("The rook is blocked. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove));
    }

    // if capture, update the captured piece on board (don't have check for the validity of toSquare as it is taken care in board.move())
    if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, false);

    // update the move on board
    board.updatePieceMoveOnBoard(movedPiece, move.fromSquare, move.toSquare);


    if(isKingUnderCheck(isPieceWhite, board)) { // is my king under check after my move is made
        // undo the move
        board.updatePieceMoveOnBoard(movedPiece, move.toSquare, move.fromSquare);

        if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, true);

        // throw error
        throw KingUnderCheckException("Cannot move piece. It is either pinned or the king is under check. Move number: " + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
    }
    
    if(isPieceWhite) {
        if(board.canWhiteKingShortCastle && move.fromSquare == 0) board.canWhiteKingShortCastle = false;
        else if(board.canWhiteKingLongCastle && move.fromSquare == 7) board.canWhiteKingLongCastle = false;
    }
    else  {
        if(board.canBlackKingShortCastle && move.fromSquare == 0) board.canBlackKingShortCastle = false;
        else if(board.canBlackKingLongCastle && move.fromSquare == 7) board.canBlackKingLongCastle = false;
    }

    uint64_t pieceAttacks = getRookAttacksForSquareAndOccupancy(move.toSquare, board.getAllPiecesBitBoard());

    CheckType check = CheckType::NO_CHECK;

    // Return the check type the move results in
    if((pieceAttacks & (1ULL << __builtin_ctzll(board.getPieceBitBoard(isPieceWhite ? Piece::BLACK_KING : Piece::WHITE_KING))))) { // if the new piece directly checks the opponent king
        // update the direct check square
        board.directCheckSquare = move.toSquare;

        check = CheckType::DIRECT_CHECK;
    }

    if(getPinDirection(!isPieceWhite, move.fromSquare, board, true) != PinDirection::NONE) { // if the move results in discovery check
        check = check == CheckType::NO_CHECK ? CheckType::DISCOVERY_CHECK : CheckType::DOUBLE_CHECK;
    }

    return check;
}

CheckType QueenMoveManager::handleMove(bool isPieceWhite, const Move& move, Board& board) {
    if(move.fromRow != move.toRow && move.fromCol != move.toCol && abs(move.fromRow - move.toRow) != abs(move.fromCol - move.toCol)) {
        // throw error
        throw InvalidMovePatternException("Invalid move pattern for a queen. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove)); 
    }

    Piece capturedPiece = board.getPieceOnBoard(move.toSquare);
    Piece movedPiece = board.getPieceOnBoard(move.fromSquare);

    // check blocks
    uint64_t pathMask = rangeMasks[move.fromSquare][move.toSquare];
    pathMask &= ~(1ULL << move.fromSquare);
    pathMask &= ~(1ULL << move.toSquare);

    if((pathMask & board.getAllPiecesBitBoard()) != 0 || (capturedPiece != Piece::EMPTY && isPieceWhite == (int)capturedPiece < 6)) {
        // throw error
        throw BlockedMoveException("The queen is blocked. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove));
    }

    // if capture, update the captured piece on board (don't have check for the validity of toSquare as it is taken care in board.move())
    if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, false);

    // update the move on board
    board.updatePieceMoveOnBoard(movedPiece, move.fromSquare, move.toSquare);


    if(isKingUnderCheck(isPieceWhite, board)) { // is my king under check after my move is made
        // undo the move
        board.updatePieceMoveOnBoard(movedPiece, move.toSquare, move.fromSquare);

        if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, true);

        // throw error
        throw KingUnderCheckException("Cannot move piece. It is either pinned or the king is under check. Move number: " + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
    }

    uint64_t pieceAttacks = getQueenAttacksForSquareAndOccupancy(move.toSquare, board.getAllPiecesBitBoard());

    CheckType check = CheckType::NO_CHECK;

    // Return the check type the move results in
    if((pieceAttacks & (1ULL << __builtin_ctzll(board.getPieceBitBoard(isPieceWhite ? Piece::BLACK_KING : Piece::WHITE_KING))))) { // if the new piece directly checks the opponent king
        // update the direct check square
        board.directCheckSquare = move.toSquare;

        check = CheckType::DIRECT_CHECK;
    }

    if(getPinDirection(!isPieceWhite, move.fromSquare, board, true) != PinDirection::NONE) { // if the move results in discovery check
        check = check == CheckType::NO_CHECK ? CheckType::DISCOVERY_CHECK : CheckType::DOUBLE_CHECK;
    }

    return check;
}

CheckType KingMoveManager::handleMove(bool isPieceWhite, const Move& move, Board& board) {
    // check move pattern
    if(abs(move.fromCol - move.toCol) == 2 && move.fromRow == move.toRow && (isPieceWhite ? move.fromSquare == 3 : move.fromSquare == 59)) {
        return handleKingCastle(move.toCol == 1, isPieceWhite, board);
    }

    if(abs(move.fromRow - move.toRow) > 1 || abs(move.fromCol - move.toCol) > 1) {
        // throw error
        throw InvalidMovePatternException("Invalid move pattern for a king. Move number: " + std::to_string(board.getMoveNumber() + 1) + std::string(". Move: ") + std::string(move.uciMove)); 
    }

    Piece capturedPiece = board.getPieceOnBoard(move.toSquare);
    Piece movedPiece = board.getPieceOnBoard(move.fromSquare);

    if(capturedPiece != Piece::EMPTY && isPieceWhite == (int)capturedPiece < 6) {
        // throw error
        throw BlockedMoveException("The king is moved to square which is occupied by same color piece. Move number: " + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
    }

    // if capture, update the captured piece on board (don't have check for the validity of toSquare as it is taken care in board.move())
    if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, false);

    // update the move on board
    board.updatePieceMoveOnBoard(movedPiece, move.fromSquare, move.toSquare);


    if(isKingUnderCheck(isPieceWhite, board)) { // is my king under check after my move is made
        // undo the move
        board.updatePieceMoveOnBoard(movedPiece, move.toSquare, move.fromSquare);

        if(capturedPiece != Piece::EMPTY) board.updatePieceCountOnBoard(capturedPiece, move.toSquare, true);

        // throw error
        throw KingUnderCheckException("Cannot move the " + std::string(isPieceWhite ? "white king." : "black king.") + " The new square is attacked." + std::string(" Move number: ") + std::to_string(board.getMoveNumber() + 1) + ". Move: " + std::string(move.uciMove));
    }

    if(isPieceWhite && board.canWhiteKingShortCastle) board.canWhiteKingShortCastle = board.canWhiteKingLongCastle = false;
    else if(!isPieceWhite && board.canBlackKingShortCastle) board.canBlackKingShortCastle = board.canBlackKingLongCastle = false; 

    // a king move can only deliver a discovery check
    if(getPinDirection(!isPieceWhite, move.fromSquare, board, true) != PinDirection::NONE) { // if the move results in discovery check
        return CheckType::DISCOVERY_CHECK;
    }

    return CheckType::NO_CHECK;
}

CheckType KingMoveManager::handleKingCastle(bool shortSide, bool isPieceWhite, Board& board) {
    if(isPieceWhite ? (shortSide ? !board.canWhiteKingShortCastle : !board.canWhiteKingLongCastle) : (shortSide ? !board.canBlackKingShortCastle : !board.canBlackKingLongCastle) ) {
        // throw error
        throw KingCastleException(std::string(isPieceWhite ? "White King " : "Black King ") +  "lost castling rights on " + std::string(shortSide ? "king side." : "queen side.") + "Move number: " + std::to_string(board.getMoveNumber() + 1)); 
    }

    int kingSquare = isPieceWhite ? 3 : 59; int kingToSquare = (shortSide ? (isPieceWhite ? 1 : 57) : (isPieceWhite ? 5 : 61));
    int rookSquare = isPieceWhite ? (shortSide ? 0 : 7) : (shortSide ? 56 : 63);
    int rookToSquare = shortSide ? kingToSquare + 1 : kingToSquare - 1;

    // mask from king square to destination king square
    uint64_t inBetweenMask = rangeMasks[kingSquare][kingToSquare];
    inBetweenMask &= ~(1ULL << kingToSquare);

    // if the king path squares are attacked, castling is not possible, throw error
    if((inBetweenMask & generateLegalAttacksForColor(!isPieceWhite, false, true, false, board) != 0)) {
        // throw error
        throw KingCastleException("There are pieces attacking " + std::string(isPieceWhite ? "White King " : "Black King ") + "on it's path of castling." + std::string("Move number: " + std::to_string(board.getMoveNumber() + 1)));
    }

    // update king move
    board.updatePieceMoveOnBoard(isPieceWhite ? Piece::WHITE_KING : Piece::BLACK_KING, kingSquare, kingToSquare);

    // update rook move
    board.updatePieceMoveOnBoard(isPieceWhite ? Piece::WHITE_ROOK : Piece::BLACK_ROOK, rookSquare, rookToSquare);

    if(isPieceWhite) board.canWhiteKingShortCastle = board.canWhiteKingLongCastle = false;
    else board.canBlackKingShortCastle = board.canBlackKingLongCastle = false;

    // only update the rooks in grid
    board.setPieceOnBoard(isPieceWhite ? Piece::WHITE_ROOK : Piece::BLACK_ROOK, rookToSquare);
    board.setPieceOnBoard(Piece::EMPTY, rookSquare);

    uint64_t pieceAttacks = getRookAttacksForSquareAndOccupancy(rookToSquare, board.getAllPiecesBitBoard());

    // castling can result only in direct checks
    //  Return the check type the move results in
    if((pieceAttacks & (1ULL << __builtin_ctzll(board.getPieceBitBoard(isPieceWhite ? Piece::BLACK_KING : Piece::WHITE_KING))))) { // if the piece directly checks the opponent king
        // update the direct check square
        board.directCheckSquare = rookToSquare;

        return CheckType::DIRECT_CHECK;
    }

    return CheckType::NO_CHECK;
}

std::shared_ptr<const MoveManagerStore> MoveManagerStore::m_pInstance = nullptr;

MoveManagerStore::MoveManagerStore() {
    m_MoveManagers[0] = std::make_shared<PawnMoveManager>();
    m_MoveManagers[1] = std::make_shared<KnightMoveManager>();
    m_MoveManagers[2] = std::make_shared<BishopMoveManager>();
    m_MoveManagers[3] = std::make_shared<RookMoveManager>();
    m_MoveManagers[4] = std::make_shared<QueenMoveManager>();
    m_MoveManagers[5] = std::make_shared<KingMoveManager>();
}

std::shared_ptr<const MoveManagerStore> MoveManagerStore::getMoveManagerStore() {
    if(m_pInstance == nullptr) return m_pInstance = std::shared_ptr<const MoveManagerStore>(new MoveManagerStore());

    return m_pInstance;
}


};
