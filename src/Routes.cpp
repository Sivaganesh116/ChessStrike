#include "Data.h"
#include "Controller.h"


void initRoutes(uWS::SSLApp & app) {
    app.ws<PlayerData>("new-game", {
        .upgrade = gameWSUpgradeHandler,
        .open = gameWSOpenHandler,
        .message = gameWSMessageHandler,
        .close = gameWSCloseHandler
    });

    app.ws<PlayerData>("rand-game", {
        .open = [](auto * ws) {
            matchPlayer(ws->getUserData(), true);
        },
        .message = randGameWSMessageHandler,
        .close = [](auto * ws, int code, std::string_view msg) {
            auto * playerData = ws->getUserData();

            playerData->gameManager_->randGameResultHandler(false, !playerData->isWhite_, "Abandonment");
        }
    })
}