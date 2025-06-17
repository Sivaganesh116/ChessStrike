#include "inc/LegalChess.h"

#include <iostream>
#include <fstream>

int main() {
    LC::compute();
    // LC::LegalChess chess("e2e4 e7e6 d2d4 d7d6 b1c3 c8d7 g1f3 c7c6 f1c4 h7h6 e1g1 f8e7 b2b4 a7a6 a2a4 g8f6 e4e5 d6e5 d4e5 f6d5 c3e4 e8g8 f3d4 e7b4 d1g4 d5c3 c1h6 g7g6 e4f6 g8h8 g4h3 c3d5 h6f8");

    for(int i = 0; i<10; i++) {
        std::ifstream uciFile("UCI.txt");

        if(!uciFile.is_open()) {
            std::cerr << "Cannot openn file" << std::endl;
        }

        int gameNumber = 1;
        std::string line;
        while(std::getline(uciFile, line)) {
            std::cout << "Game: " << gameNumber << std::endl;
            LC::LegalChess chess(line);
            gameNumber++;
        }

        uciFile.close();
    }

    return 0;
}