#include "Data.h"
#include "Controller.h"

#include <fstream>
#include <iostream>

void initRoutes(uWS::SSLApp & app) {
    app.ws<PlayerData>("/new-game", {
        .upgrade = gameWSUpgradeHandler,
        .open = gameWSOpenHandler,
        .message = gameWSMessageHandler,
        .close = gameWSCloseHandler
    });


    app.ws<PlayerData>("/rand-game", {
        .open = [](auto * ws) {
            matchPlayer(ws->getUserData(), true);
        },
        .message = randGameWSMessageHandler,
        .close = [](auto * ws, int code, std::string_view msg) {
            auto * playerData = ws->getUserData();

            playerData->gameManager_->randGameResultHandler(false, !playerData->isWhite_, "Abandonment");
        }
    });


    app.ws<GameManagerPointer>("/watch", {
        .upgrade = watchWSUpgradeHandler,
        .open = [](auto * ws) {
            GameManager* gamePointer = ws->getUserData()->pointer;
            ws->subscribe(std::to_string(gamePointer->gameID_));
        }
    });


    app.get("/api/user/status", [](auto *res, auto *req) {
        // API ENDPOINT for dynamic data
        res->writeHeader("Content-Type", "application/json");

        auto cookies = parseCookies(req->getHeader("cookie"));
        auto it = cookies.find("token");

        if(it == cookies.end()) {
            res->writeHeader("Content-Type", "application/json")->end(R"({"loggedIn": false})");
            return;
        }

        auto [id, username] = getUserNameIDFromToken(it->second);

        res->writeHeader("Content-Type", "application/json")->end(R"({"loggedIn": true, "username": ")" + username + R"("})");
    });


    app.get("/*", [](auto *res, auto *req) {
        // This is your CATCH-ALL ROUTE for serving static files
        std::string url = std::string(req->getUrl());
        std::string filePath;

        // If root is requested, serve home.html
        if (url == "/") {
            filePath = "/home/sivaganesh116/Workspace/ChessStrike/public/html/home.html";
        }
        else {
            // Otherwise, construct the path from the URL
            // IMPORTANT: In a real app, sanitize this path to prevent directory traversal attacks (e.g., block '../')
            filePath = "/home/sivaganesh116/Workspace/ChessStrike/public" + url;
        }

        serveFile(filePath, res);
        res->end();
    });


    app.get("/game/:id", [](auto * res, auto * req) {
        serveFile("/home/sivaganesh116/Workspace/ChessStrike/public/html/game.html", res);
        res->end();
    });

    // server different js file based ono the role of the user
    app.get("/game/:id/game.js", [](auto * res, auto * req) {
        std::string sGameID(req->getParameter("id"));

        auto cookies = parseCookies(req->getHeader("cookie"));
        auto it = cookies.find("token");

        // A user who is not logged in, he can watch or view
        if(it == cookies.end()) {
            // anonymous user tried to create new registered game
            if(sGameID == "new") {
                res->writeStatus("401 Not Authorised")->end();
            }
            else if(sGameID.length() > 0 && std::all_of(sGameID.begin(), sGameID.end(), ::isdigit)) {
                int gameID = stoi(sGameID);

                auto liveGameIter = liveGames.find(gameID);

                // not a live game
                if(liveGameIter == liveGames.end()) {
                    serveFile("/home/sivaganesh116/Workspace/ChessStrike/public/js/viewer.js", res);
                    res->end();
                }
                else {
                    serveFile("/home/sivaganesh116/Workspace/ChessStrike/public/js/watcher.js", res);
                    res->end();
                }
            }
            else {
                res->writeStatus("404 Bad Request")->writeHeader("Content-Type", "text/plain")->end("Invalid game requested");
            }
            
            return;
        }

        auto [id, username] = getUserNameIDFromToken(it->second);

        if(id == -1) {
            res->writeStatus("401 Not Authorised")->end();
            std::cout << "Fishy token\n";
            return;
        }

        // a logged in user he can be playing in the requested game
        if(sGameID.length() > 0 && std::all_of(sGameID.begin(), sGameID.end(), ::isdigit)) {
            int gameID = stoi(sGameID);

            auto liveGameIter = liveGames.find(gameID);

            // not a live game
            if(liveGameIter == liveGames.end()) {
                serveFile("/home/sivaganesh116/Workspace/ChessStrike/public/js/viewer.js", res);
                res->end();
            }
            else {
                auto playersInGameIter = playersInGame.find(id);
                if(playersInGameIter != playersInGame.end()) {
                    auto * gameManager = playersInGameIter->second;
                    if(gameManager->gameID_ == gameID) {
                        serveFile("/home/sivaganesh116/Workspace/ChessStrike/public/js/player.js", res);
                        res->end();
                    }
                }
                else {
                    serveFile("/home/sivaganesh116/Workspace/ChessStrike/public/js/watcher.js", res);
                    res->end();
                }
            }
        }
        else {
            if(sGameID == "new") {
                serveFile("/home/sivaganesh116/Workspace/ChessStrike/public/js/player.js", res);
                res->end();
            }
            else 
                res->writeStatus("404 Bad Request")->writeHeader("Content-Type", "text/plain")->end("Invalid game requested");
        }

    });


    app.post("/login", [](auto * res, auto * req) { 
        std::shared_ptr<bool> aborted(new bool(false));

        res->onAborted([aborted]() {
            *aborted = true;
        });

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

        res->onData([res, aborted] (std::string_view body, bool isLast) {
            handleSignupOrLogin(aborted, res, body, false);
        });
    });


    app.post("/signup", [](auto * res, auto * req) {
        std::shared_ptr<bool> aborted(new bool(false));

        res->onAborted([aborted]() {
            *aborted = true;
        });

        auto cookies = parseCookies(req->getHeader(("cookie")));
        auto it = cookies.find("token");

        if(it != cookies.end()) {
            std::string token = it->second;

            auto [id, username] = getUserNameIDFromToken(token);

            if(id != -1) {
                res->writeStatus("404 Bad Request")->end("Already logged in");
                return;
            }
        }
        std::string body = "";

        res->onData([&body, res, aborted](std::string_view chunk, bool isLast) {
            body.append(std::string(chunk));

            if(isLast) {
                handleSignupOrLogin(aborted, res, body, true);
            }
        });
    });


    app.get("/games", [](auto * res, auto * req) {
        std::shared_ptr<bool> aborted(new bool(false));

        res->onAborted([aborted](){
            *aborted = true;
        });

        int numGames = 10, batchNumber = 0;
        std::string batchString(req->getQuery("batch"));

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
                if(!*aborted)
                    res->writeStatus("401 Not Authorised")->write("Please log in to access your game history.");
                
                return;
            }

            gameHistoryHandler(aborted, res, id, batchNumber, numGames);
        }
        else {
            if(!*aborted)
                res->writeStatus("401 Not Authorised")->write("Please log in to access your game history.");
        }
    });


    app.get("/live-games", [](auto * res, auto * req) {
        std::shared_ptr<bool> aborted(new bool(false));

        res->onAborted([aborted]() {
            std::cout << "live-games aborted\n";
            *aborted = true;
        });

        int numGames = 10;

        std::string sCount(req->getQuery("count"));
        if(sCount.length() > 0 && std::all_of(sCount.begin(), sCount.end(), ::isdigit)) {
            numGames = stoi(sCount);
        }

        getLiveGamesHandler(aborted, res, 10);
    });


    app.get("/player/:username", [](auto * res, auto * req) {
        std::shared_ptr<bool> aborted(new bool(false));

        res->onAborted([aborted]() {
            std::cout << "player profile aborted\n";
            *aborted = true;
        });

        std::string player(req->getParameter("username"));
        getPlayerProfile(aborted, res, player);
    });


    app.get("/live-game/:user_id", [](auto * res, auto * req) {
        std::shared_ptr<bool> aborted(new bool(false));

        res->onAborted([aborted]() {
            std::cout << "live-game aborted\n";
            *aborted = true;
        });

        std::string sUserId(req->getParameter("user_id"));

        if(sUserId.length() > 0 && std::all_of(sUserId.begin(), sUserId.end(), ::isdigit))
            res->writeHeader("Content-Type", "text/plain")->end(getLiveGameOfUser(stoi(sUserId)));
        else 
            res->writeStatus("404 Not Found")->writeHeader("Content-Type", "text/plain")->end("Invalid user id to get live game");
    });

    app.get("/game-details/:id", [](auto * res, auto * req) {
        std::shared_ptr<bool> aborted(new bool(false));

        res->onAborted([aborted]() {
            std::cout << "game aborted\n";
            *aborted = true;
        });

        std::string sGameId(req->getParameter("id"));
        int gameID;

        if(sGameId.length() > 0 && std::all_of(sGameId.begin(), sGameId.end(), ::isdigit)) {
            gameID = stoi(sGameId);

            auto cookies = parseCookies(req->getHeader("cookie"));
            auto it = cookies.find("token");

            if(it == cookies.end()) {
                getGameHandler(aborted, res, gameID, -1);
            }
            else {
                auto [uid, username] = getUserNameIDFromToken(it->second);
                getGameHandler(aborted, res, gameID, uid);
            }
        }    
        else  {
            if((!*aborted))
                res->writeStatus("404 Not Found")->writeHeader("Content-Type", "text/plain")->end("Invalid game id");
            return;
        }
    });
}