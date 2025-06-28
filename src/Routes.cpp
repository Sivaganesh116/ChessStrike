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
    });


    app.ws<GameManagerPointer>("watch", {
        .upgrade = watchWSUpgradeHandler,
        .open = [](auto * ws) {
            GameManager* gamePointer = ws->getUserData()->pointer;
            ws->subscribe(std::to_string(gamePointer->gameID_));
        }
    });


    app.get("/home", [](auto * res, auto * req) {
        res->onData(httpResOnDataHandler);
    });


    app.get("/login", [](auto * res, auto * req) { 
        auto cookies = parseCookies(req->getHeader(("cookie")));
        auto it = cookies.find("token");

        if(it != cookies.end()) {
            std::string token = it->second;

            auto [id, username] = getUserNameIDFromToken(token);

            if(id != -1) {
                res->writeStatus("404 Bad Request")->write("Already logged in");
                return;
            }
        }

        res->onData([res] (std::string_view body, bool isLast) {
            handleSignupOrLogin(res, body, false);
        });
    });


    app.get("/signup", [](auto * res, auto * req) {
        auto cookies = parseCookies(req->getHeader(("cookie")));
        auto it = cookies.find("token");

        if(it != cookies.end()) {
            std::string token = it->second;

            auto [id, username] = getUserNameIDFromToken(token);

            if(id != -1) {
                res->writeStatus("404 Bad Request")->write("Already logged in");
                return;
            }
        }

        res->onData([res](std::string_view body, bool isLast) {
            handleSignupOrLogin(res, body, true);
        });
    });


    app.get("/games:batch", [](auto * res, auto * req) {
        int numGames = 10, batchNumber = 0;
        std::string batchString(req->getParameter("batch"));
        if(batchString.length() != 0) {
            try{
                batchNumber = stoi(batchString);
                numGames = 20;
            }
            catch(std::exception& e) {
                numGames = 10;
                std::cout << "Invalid Batch Number: " << batchNumber << std::endl;
            }
        }

        auto cookies = parseCookies(req->getHeader(("cookie")));
        auto it = cookies.find("token");

        if(it != cookies.end()) {
            std::string token = it->second;

            auto [id, username] = getUserNameIDFromToken(token);

            if(id == -1) {
                res->writeStatus("401 Not Authorised")->write("Please log in to access your game history.");
                return;
            }

            gameHistoryHandler(res, id, batchNumber, numGames);
        }
        else {
            res->writeStatus("401 Not Authorised")->write("Please log in to access your game history.");
            return;
        }
    });


    app.get("/live-games", [](auto * res, auto * req) {
        int numGames = 10;
        getLiveGamesHandler(res, 10);
    });


    app.get("/player:username", [](auto * res, auto * req) {
        std::string player(req->getParameter("username"));
        getPlayerProfile(res, player);
    });


    app.get("/live-game:user_id", [](auto * res, auto * req) {
        std::string sUserId(req->getParameter("user_id"));

        if(sUserId.length() > 0 && std::all_of(sUserId.begin(), sUserId.end(), ::isdigit))
            res->write(getLiveGameOfUser(stoi(sUserId)));
        else 
            res->write("Invalid user id");
    });

    app.get("/game:id", [](auto * res, auto * req) {
        std::string sGameId(req->getParameter("id"));
        int gameID;

        if(sGameId.length() > 0 && std::all_of(sGameId.begin(), sGameId.end(), ::isdigit)) {
            gameID = stoi(sGameId);

            auto cookies = parseCookies(req->getHeader("cookie"));
            auto it = cookies.find("token");

            if(it == cookies.end()) {
                getGameHandler(res, gameID, -1);
            }
            else {
                auto [uid, username] = getUserNameIDFromToken(it->second);
                getGameHandler(res, gameID, uid);
            }
        }    
        else  {
            res->write("Invalid game id");
            return;
        }
    });
}