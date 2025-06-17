#include <iostream>
#include <pqxx/pqxx>
#include <uWebSockets/App.h>

#include "LegalChess.h"
#include <string>

PlayerData* waitingPlayer = nullptr;

struct PlayerData {
    std::string name;
    int player_id;
    uWS::WebSocket<true, true, LC::LegalChess> ws;
};

void startGame(PlayerData*& waitingPlayer, PlayerData* newPlayer) {
    
    // reset the waiting player
    waitingPlayer = nullptr;
}

void registerRoutes(uWS::SSLApp & app) {
    app.ws<PlayerData>("new-game", {
        .open = [](auto* ws) {
            auto * playerData = ws->getUserData();

            if(waitingPlayer == nullptr) waitingPlayer = playerData;
            else startGame(waitingPlayer, playerData);
        }
    });
}

int main() {
    uWS::SSLApp app;    

    return 0;
}