#include <iostream>
#include <pqxx/pqxx>
#include <uWebSockets/App.h>
#include <nlohmann/json.hpp>

#include "LegalChess.h"
#include <string>
#include <sstream>
#include <fstream>

struct PlayerData {
    std::string name;
    int id;
    std::shared_ptr<LC::LegalChess> chess;
    uWS::WebSocket<true, true, PlayerData>* ws, *otherWs;
    bool isWhite;


    PlayerData() {
        name = "";
        id = -1;
        chess = nullptr;
        ws = nullptr;
        otherWs = nullptr;
        isWhite = false;
    }
};

using json = nlohmann::json;

PlayerData* waitingPlayer = nullptr;

void serveFile(std::string fileName, std::string ext, uWS::HttpResponse<true>* res) {
    if(!ext.size()) return;
    if(!fileName.size()) return;

    std::ifstream file(fileName + ext);

    if(!file.is_open()) {
        std::cout << "Cannot open file: " << fileName + ext << std::endl;
        return;
    }

    std::string fileData = "";
    std::string line = "";

    while(std::getline(file, line)) {
        fileData += line;
    }

    res->writeHeader("Content-Type", ext);
    res->end(fileData);
}

void startGame(PlayerData* newPlayer) {
    std::cout << "Start - game" << std::endl;
    // Create a random device and a random number generator
    std::random_device rd;  // Seed
    std::mt19937 gen(rd()); // Mersenne Twister engine

    // Define the distribution range [1, 20]
    std::uniform_int_distribution<> dist(1, 20);

    // Generate random number
    int random_number = dist(gen);

    if(random_number % 2) {
        waitingPlayer->name = "white";
        waitingPlayer->id = 1;
        waitingPlayer->isWhite = true;

        newPlayer->name = "black";
        newPlayer->id = 0;
        newPlayer->isWhite = false;
    }
    else {
        waitingPlayer->name = "black";
        waitingPlayer->id = 0;
        waitingPlayer->isWhite = false;

        newPlayer->name = "white";
        newPlayer->id = 1;
        newPlayer->isWhite = true;
    }

    waitingPlayer->chess = newPlayer->chess = std::make_shared<LC::LegalChess>();
    waitingPlayer->otherWs = newPlayer->ws;
    newPlayer->otherWs = waitingPlayer->ws;

    json toNewPlayer;
    toNewPlayer["type"] = "start-game";
    toNewPlayer["white"] = newPlayer->isWhite;

    json toWaitingPlayer;
    toWaitingPlayer["type"] = "start-game";
    toWaitingPlayer["white"] = waitingPlayer->isWhite;

    std::cout << "player's matched" << std::endl;

    newPlayer->ws->send(toNewPlayer.dump(), uWS::OpCode::TEXT);
    waitingPlayer->ws->send(toWaitingPlayer.dump(), uWS::OpCode::TEXT);

    // reset the waiting player
    waitingPlayer = nullptr;
}

void registerRoutes(uWS::SSLApp & app) {
    app.get("/home", [](auto * res, auto * req) {
        serveFile("views/index", ".html", res);
    });

    app.ws<PlayerData>("/rand-game", {
        .open = [](auto* ws) {
            std::cout << "Rand-game requested\n";
            auto * playerData = ws->getUserData();
            playerData->ws = ws;

            if(waitingPlayer == nullptr) waitingPlayer = playerData;
            else startGame(playerData);
        },

        .message = [](auto* ws, std::string_view msg, uWS::OpCode code) {
            auto * playerData = ws->getUserData();
            auto & chess = ws->getUserData()->chess;

            std::stringstream ss{std::string(msg)};

            std::string msgType;
            ss >> msgType;

            if(msgType == "move") {
                try {
                    std::string move;
                    ss >> move;

                    chess->makeMove(move);

                    playerData->ws->send("move:true", code);
                    playerData->otherWs->send("newmove:" + move, code);
                    
                    if(chess->isGameOver()) {
                        json resultJson;
                        resultJson["type"] = "result";

                        if(chess->isCheckMate(!playerData->isWhite)) {
                            resultJson["result"] = "win";
                            resultJson["reason"] = "Checkmate";
                            playerData->ws->send(resultJson.dump(), code);

                            resultJson["result"] = "lose";
                            resultJson["reason"] = "Checkmate";
                            playerData->otherWs->send(resultJson.dump(), code);
                        }
                        else {
                            resultJson["result"] = "draw";
                            
                            if(chess->isStalemate()) resultJson["reason"] = "Stalemate";
                            else if(chess->isDrawByInsufficientMaterial()) resultJson["reason"] = "Insufficient Material";
                            else if(chess->isDrawByRepitition()) resultJson["reason"] = "Repitition";
                            else if(chess->isDrawBy50MoveRule()) resultJson["reason"] = "50 Half Moves";

                            playerData->ws->send(resultJson.dump(), code);
                            playerData->otherWs->send(resultJson.dump(), code);
                        }
                    }

                } catch(std::exception& e) {
                    ws->send("move:false", code);
                    ws->send(std::string("Error:") + e.what(), code);
                }
            }
        }
    });
}

int main() {
    LC::compute();

    uWS::SSLApp app;
    
    registerRoutes(app);

    app.listen("0.0.0.0", 8080, [](auto * socket){
        if(!socket) std::cout << "Couldn't start the sever\n";
        else std::cout << "Server running at port 8080\n";
    });

    app.run();

    return 0;
}