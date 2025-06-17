#ifndef __LEGAL_CHESS_H__
#define __LEGAL_CHESS_H__

#include "Board.h"

#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <sstream>
#include <chrono>
#include <climits>

namespace LC {

class Board;
struct Move;

class LegalChess {
public:
    LegalChess() : m_pBoard(std::make_unique<Board>()) {}
    LegalChess(std::string uciMoves) : m_pBoard(std::make_unique<Board>()) {

        std::stringstream ss(uciMoves);
        std::string move;

        long long totalTime = 0;
        long minTime = LONG_MAX;
        long maxTime = LONG_MIN;

        while(ss >> move) {
            auto start = std::chrono::high_resolution_clock::now();

            makeMove(move);

            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

            totalTime += duration;
            minTime = std::min(minTime, duration);
            maxTime = std::max(maxTime, duration);
        }

        std::cout << gameResultToString[(int)m_pBoard->getGameResult()] << std::endl;
        std::cout << "Average Time: " << totalTime/m_pBoard->getMoveNumber() << std::endl;
        std::cout << "Max Time: " << maxTime << std::endl;
        std::cout << "Min Time: " << minTime << std::endl;
        std::cout << std::endl;
    }

    ~LegalChess() = default;

    GameResult makeMove(std::string move) {
        if(m_pBoard->getGameResult() != GameResult::IN_PROGRESS) {
            throw GameOverException("Game is Over. Game Result: " + std::string(gameResultToString[(int)m_pBoard->getGameResult()]) + ". Cannot make move. Move number: " + std::to_string(m_pBoard->getMoveNumber() + 1) + ". Move: " + move);
        }

        Move sMove;

        validateMove(move, sMove);

        if(move.length() == 4)
            m_pBoard->move(sMove);
        else 
            m_pBoard->promote(move[4], sMove);

        return m_pBoard->getGameResult();
    }   

    bool isGameOver() {
        return m_pBoard->isGameOver();
    }

    bool isCheckMate(bool white) {
        return m_pBoard->isCheckmate(white);
    }

    bool isStalemate() {
        return m_pBoard->isStalemate();
    }

    bool isDrawByRepitition() {
        return m_pBoard->isDrawByRepitition();
    }

    bool isDrawByInsufficientMaterial() {
        return m_pBoard->isDrawyByInsufficientMaterial();
    }

    bool isDrawBy50MoveRule() {
        return m_pBoard->isDrawBy50HalfMoves();
    }

    GameResult getGameResult() {
        return m_pBoard->getGameResult();
    }

    bool doesColorHaveInsufficientMaterial(bool white) {
        return m_pBoard->doesColorHaveInsufficientMaterial(white);
    }

    std::string getFENString() {
        return m_pBoard->getFENString();
    }

    std::vector<std::vector<char>> getBoard() {
        return m_pBoard->getBoard();
    }


private:
    void validateMove(std::string& move, Move& sMove) {
        int file1 = move[0], rank1 = move[1], file2 = move[2], rank2 = move[3];

        if((move.length() != 4 && move.length() != 5) || file1 < 'a' || file1 > 'h' || file2 < 'a' || file2 > 'h' || rank1 < '1' || rank1 > '8' || rank2 < '1' || rank2 > '8') {
            throw InvalidMoveException("The move is invalid. Make sure to follow UCI Notation. Move number: " + std::to_string(m_pBoard->getMoveNumber() + 1) + ". Move: " + move);
        }

        sMove.fromRow = rank1 - '1';
        sMove.fromCol = 'h' - file1;
        sMove.toRow = rank2 - '1';
        sMove.toCol = 'h' - file2;
        sMove.fromSquare = sMove.fromRow*8 + sMove.fromCol;
        sMove.toSquare = sMove.toRow*8 + sMove.toCol;
        sMove.uciMove = move;
    }


    std::unique_ptr<Board> m_pBoard;
};


};

#endif