#include "App.h"
#include "jwt.h"
#include "nlohmann/json.hpp"


#include "Data.h"
#include "ThreadPool.h"
#include "ConnectionPool.h"

#include <string>
#include <fstream>

using json = nlohmann::json;
PlayerData* randWaitingPlayer = nullptr, *regWaitingPlayer = nullptr;

const std::string connStr = "host=localhost port=5432 dbname=chess user=postgres password=yourpassword";
PostgresConnectionPool cPool(connStr, 10);
ThreadPool tPool(2);

// JWT secret key
const std::string jwtSecret = "your_secret_key";

std::unordered_map<int, PlayerData*> closedConnections;
std::unordered_map<int, GameManager*> liveGames;
std::unordered_map<int, GameManager*> playersInGame;


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

std::string createJWT(int userID, const std::string& username) {
    auto token = jwt::create()
        .set_type("JWT")
        .set_issuer("chess-strike")
        .set_payload_claim("username", jwt::claim(username))
        .set_payload_claim("id", jwt::claim(std::to_string(userID)))
        .sign(jwt::algorithm::hs256{jwtSecret});
    return token;
}


std::string getTokenFromReq(uWS::HttpRequest * req) {
    std::string_view authHeader = req->getHeader("authorization");

    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") {
        return "";
    }

    std::string token = std::string(authHeader.substr(7));  // Skip "Bearer "

    return token;
}


void matchPlayer(PlayerData* newPlayerData, bool randGame) {
    PlayerData** waitingPlayerRef = randGame ? &randWaitingPlayer : &regWaitingPlayer;

    PlayerData* waitingPlayer = *waitingPlayerRef;
    

    // Create a random device and a random number generator
    std::random_device rd;  // Seed
    std::mt19937 gen(rd()); // Mersenne Twister engine

    // Define the distribution range [1, 20]
    std::uniform_int_distribution<> dist(1, 20);

    // Generate random number
    int random_number = dist(gen);

    if(random_number % 2) {
        if(waitingPlayer->name_ == "") waitingPlayer->name_ = "white";
        waitingPlayer->isWhite_ = true;

        if(newPlayerData->name_ == "") newPlayerData->name_ = "black";
        newPlayerData->isWhite_ = false;
    }
    else {
        if(waitingPlayer->name_ == "") waitingPlayer->name_ = "black";
        waitingPlayer->isWhite_ = false;

        if(newPlayerData->name_ == "") newPlayerData->name_ = "white";
        newPlayerData->isWhite_ = true;
    }

    if(waitingPlayer->chess_) newPlayerData->chess_ = waitingPlayer->chess_;
    else if(newPlayerData->chess_) waitingPlayer->chess_ = newPlayerData->chess_;
    else waitingPlayer->chess_ = newPlayerData->chess_ = std::make_shared<LC::LegalChess>();

    waitingPlayer->otherWS_ = newPlayerData->ws_;
    newPlayerData->otherWS_ = waitingPlayer->ws_;

    if(!waitingPlayer->moveTimer_) waitingPlayer->moveTimer_ = std::make_unique<MoveTimer>(300, (uv_loop_t*)uWS::Loop::get(), waitingPlayer);
    if(!newPlayerData->moveTimer_) newPlayerData->moveTimer_ = std::make_unique<MoveTimer>(300, (uv_loop_t*)uWS::Loop::get(), newPlayerData);

    waitingPlayer->inGame_ = newPlayerData->inGame_ = true;

    tPool.submit([waitingPlayerRef, waitingPlayer, newPlayerData, randGame](){
        int gameID = -1;

        if(randGame) {
            auto connHandle = cPool.acquire();
            pqxx::work txn(*connHandle->get());

            pqxx::result txnResult = txn.exec(
                pqxx::zview("INSERT INTO game (white_name, black_name) "
                "VALUES ($1, $2) RETURNING id"),
                pqxx::params(waitingPlayer->isWhite_ ? waitingPlayer->name_ : newPlayerData->name_, waitingPlayer->isWhite_ ? waitingPlayer->name_ : newPlayerData->name_)
            );

            if(txnResult.empty()) {
                std::cerr << "Couldn't fetch new game_id after insertion." << std::endl;
                return;
            }

            gameID = txnResult[0][0].as<int>();

            txn.commit();
        }

        json toNewPlayer;
        toNewPlayer["type"] = "start-game";
        toNewPlayer["white"] = newPlayerData->isWhite_;

        json toWaitingPlayer;
        toWaitingPlayer["type"] = "start-game";
        toWaitingPlayer["white"] = waitingPlayer->isWhite_;

        std::cout << "player's matched" << std::endl;

        newPlayerData->ws_->send(toNewPlayer.dump(), uWS::OpCode::TEXT);
        waitingPlayer->ws_->send(toWaitingPlayer.dump(), uWS::OpCode::TEXT);

        if(waitingPlayer->isWhite_) {
            waitingPlayer->gameManager_ = newPlayerData->gameManager_ = std::make_shared<GameManager>(gameID, waitingPlayer, newPlayerData);
            waitingPlayer->moveTimer_->start();
        }
        else {
            waitingPlayer->gameManager_ = newPlayerData->gameManager_ = std::make_shared<GameManager>(gameID, newPlayerData, waitingPlayer);
            newPlayerData->moveTimer_->start();
        }

        playersInGame.insert({waitingPlayer->id_, waitingPlayer->gameManager_.get()});
        playersInGame.insert({newPlayerData->id_, newPlayerData->gameManager_.get()});

        liveGames.insert({gameID, waitingPlayer->gameManager_.get()});

        // reset the waiting player
        *waitingPlayerRef = nullptr;
    });
}


// Helper to parse cookie string
std::unordered_map<std::string, std::string> parseCookies(std::string_view cookieHeader) {
    std::unordered_map<std::string, std::string> cookies;
    size_t pos = 0;
    while (pos < cookieHeader.size()) {
        size_t eq = cookieHeader.find('=', pos);
        if (eq == std::string_view::npos) break;
        size_t end = cookieHeader.find(';', eq);
        if (end == std::string_view::npos) end = cookieHeader.size();
        std::string key = std::string(cookieHeader.substr(pos, eq - pos));
        std::string value = std::string(cookieHeader.substr(eq + 1, end - eq - 1));
        cookies[key] = value;
        pos = end + 2; // skip "; "
    }

    return cookies;
}

std::pair<int, std::string> getUserNameIDFromToken(std::string token) {
    std::string username = "";
    int id = -1;

    try {
        auto decoded = jwt::decode(token);

        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{jwtSecret})
            .with_issuer("chess-server");

        verifier.verify(decoded);  // Throws if verification fails

        username = decoded.get_payload_claim("username").as_string();
        id = stoi(decoded.get_payload_claim("id").as_string());

    } catch (const std::exception& e) {
        std::cout << "Invalid token: " << e.what() << std::endl;
    }

    return {id, username};
}


void httpResOnDataHandler(std::string_view data, bool isLast) {
    // if(isLast) {
    //     // check if the user is already logged in
    //     auto cookies = parseCookies(req->getHeader(("cookie")));
    //     auto it = cookies.find("token");

    //     if(it == cookies.end()) {
    //         // To-Do: serve signup version of page
    //         return;
    //     }

    //     std::string token = it->second;

    //     auto [id, username] = getUserNameIDFromToken(token);

    //     if(id == -1) {
    //         // To-Do: serve signup version of page
    //         return;
    //     }

    //     // To-Do: serve logged in version of page
    // }
}


void gameWSUpgradeHandler(uWS::HttpResponse<true> * res, uWS::HttpRequest * req, struct us_socket_context_t * context) {
    bool connectionAborted = false;

    res->onAborted([&connectionAborted]() {
        connectionAborted = true;
    });

    // first check if the user is authorized to create a websocket connection
    auto cookies = parseCookies(req->getHeader(("cookie")));
    auto it = cookies.find("token");

    if(it == cookies.end()) {
        std::cout << "Not Authorized to create web socket connection.\n";
        return;
    }

    std::string token = it->second;

    auto [id, username] = getUserNameIDFromToken(token);

    if(id == -1) {
        std::cout << "Not Authorized to create web socket connection.\n";
    }


    /* If we have this header set, it's a websocket */
    std::string_view secWebSocketKey = req->getHeader("sec-websocket-key");
    if (secWebSocketKey.length() == 24) {
        std::string_view secWebSocketProtocol = req->getHeader("sec-websocket-protocol");
        std::string_view secWebSocketExtensions = req->getHeader("sec-websocket-extensions");

        auto it = closedConnections.find(id);

        if(it != closedConnections.end()) {
            auto* playerData = it->second;
            closedConnections.erase(it);

            // stop the running abandon timer
            playerData->stopAbandonTimer();

            if(!connectionAborted) 
                res->template upgrade<PlayerData>(std::move(*playerData), secWebSocketKey, secWebSocketProtocol, secWebSocketExtensions, context);
            else std::cout << "Connection Aborted before upgrade\n";
        }
        else if(playersInGame.find(id) != playersInGame.end()) {
            // To-Do: should i refuse the connection or let it play with this new connection?
            res->end("You are already playing in the requested game. Please switch back to the running game's tab.");
            return;
        }
        else {
            PlayerData playerData;
            playerData.name_ = username;
            playerData.id_ = id;

            if(!connectionAborted) 
                res->template upgrade<PlayerData>(std::move(playerData), secWebSocketKey, secWebSocketProtocol, secWebSocketExtensions, context);
            else std::cout << "Connection Aborted before upgrade\n";
        }
    }
    else {
        std::cout << "Couldn't create web socket connection. Invalid Web Socket Key.\n";
        /* Tell the router that we did not handle this request */
        req->setYield(true);
    }
}



void gameMoveHandler(uWS::WebSocket<true, true, PlayerData>* ws, PlayerData* movedPlayerData, std::string move) {
    auto & chess = movedPlayerData->chess_;

    try {
        chess->makeMove(move);
    } catch(std::exception& e) {
        std::cout << "Invalid Move Received: " << e.what() << std::endl;
        return;
    }
    movedPlayerData->otherWS_->send("move " + move, uWS::OpCode::TEXT);
    ws->publish(movedPlayerData->gameManager_->sGameID_, "move " + move, uWS::OpCode::TEXT);

    if(chess->isGameOver()) {
        bool isDraw = false, whiteWon = false;
        std::string reason;
        std::string dbReason;
        
        // if opponent is checkmated
        if(chess->isCheckMate(!movedPlayerData->isWhite_)) {
            reason = "Checkmate";
            dbReason = 'c';
            whiteWon = movedPlayerData->isWhite_ == true;
        }
        // game is drawn
        else {
            isDraw = true;

            if(chess->isStalemate()) reason = "Stalemate", dbReason = "s";
            else if(chess->isDrawByInsufficientMaterial()) reason = "Insufficient Material", dbReason = "i";
            else if(chess->isDrawByRepitition()) reason = "Repitition", dbReason = "r";
            else if(chess->isDrawBy50MoveRule()) reason = "50 Half Moves", dbReason = "f";
        }

        PlayerData *whiteData, *blackData;

        if(movedPlayerData->isWhite_) {
            whiteData = movedPlayerData;
            blackData = movedPlayerData->otherWS_->getUserData();
        }
        else {
            blackData = movedPlayerData;
            whiteData = movedPlayerData->otherWS_->getUserData();
        }

        movedPlayerData->gameManager_->gameResultHandler(isDraw, whiteWon, reason, dbReason);
    }
}


void rematch(PlayerData* p1, PlayerData* p2) {
    p1->inGame_ = p2->inGame_ = true;

    // Create a random device and a random number generator
    std::random_device rd;  // Seed
    std::mt19937 gen(rd()); // Mersenne Twister engine

    // Define the distribution range [1, 20]
    std::uniform_int_distribution<> dist(1, 20);

    // Generate random number
    int random_number = dist(gen);

    if(random_number % 2) {
        p1->isWhite_ = true;
        p2->isWhite_ = false;
    }
    else {
        p1->isWhite_ = false;
        p2->isWhite_ = true;
    }

    tPool.submit([p1, p2](){
        auto connHandle = cPool.acquire();
        pqxx::work txn(*connHandle->get());

        pqxx::result txnResult = txn.exec(
            pqxx::zview("INSERT INTO game (white_name, black_name) VALUES ($1, $2) RETURNING id"),
            pqxx::params((p1->isWhite_ ? p1->name_ : p2->name_), (p2->isWhite_ ? p1->name_ : p2->name_))
        );

        if(txnResult.empty()) {
            std::cerr << "Couldn't fetch new game_id after insertion." << std::endl;
            return;
        }

        txn.commit();

        json toP1;
        toP1["type"] = "start-game";
        toP1["white"] = p1->isWhite_;

        json toP2;
        toP2["type"] = "start-game";
        toP2["white"] = p2->isWhite_;

        std::cout << "player's re-matched" << std::endl;

        p1->ws_->send(toP1.dump(), uWS::OpCode::TEXT);
        p2->ws_->send(toP2.dump(), uWS::OpCode::TEXT);


        // It is assumed that all the relvant data of both players and game is reset

        // publish rematch to subscribers/watchers
        json rematchJson;
        rematchJson["type"] = "rematch";
        rematchJson["id"] = txnResult[0][0].as<int>();
        rematchJson["white"] = p1->isWhite_ ? p1->id_ : p2->id_;
        rematchJson["black"] = p1->isWhite_ ? p2->id_ : p1->id_;

        p1->ws_->publish(p1->gameManager_->sGameID_, rematchJson.dump(), uWS::OpCode::TEXT);

        p1->gameManager_->gameID_ = txnResult[0][0].as<int>();
        p1->gameManager_->sGameID_ = std::to_string(p1->gameManager_->gameID_);

        if(p1->isWhite_) {
            p1->gameManager_->whiteData_ = p1;
            p1->gameManager_->blackData_ = p2;
            p1->moveTimer_->start();
        }
        else {
            p1->gameManager_->whiteData_ = p2;
            p1->gameManager_->blackData_ = p1;
            p2->moveTimer_->start();
        }

        playersInGame.insert({p1->id_, p1->gameManager_.get()});
        playersInGame.insert({p2->id_, p2->gameManager_.get()});

        liveGames.insert({txnResult[0][0].as<int>(), p1->gameManager_.get()});
    });

}


void gameWSMessageHandler(uWS::WebSocket<true, true, PlayerData>* ws, std::string_view msg, uWS::OpCode code) {
    auto* movedPlayerData = ws->getUserData();

    std::stringstream ss{std::string(msg)};
    std::string msgType;
    ss >> msgType;

    if(msgType == "move") {
        if(!movedPlayerData->inGame_) return;

        auto & chess = ws->getUserData()->chess_;

        std::string move;
        ss >> move;

        gameMoveHandler(ws, movedPlayerData, move);
    }
    else if(msgType == "chat") {
        if(!movedPlayerData->otherWS_ || movedPlayerData->otherWS_->getUserData()->inGame_) return;

        ws->getUserData()->otherWS_->send(msg);
    }
    else if(msgType == "draw") {
        if(!movedPlayerData->inGame_) return;

        movedPlayerData->offeredDraw_ = !movedPlayerData->offeredDraw_;
        ws->getUserData()->otherWS_->send(msg);
    }
    else if(msgType == "result") {
        if(!movedPlayerData->inGame_) return;

        std::string result;
        ss >> result;

        // To-Do: does the order of player data not matter?
        if(result == "d") {
            if(movedPlayerData->otherWS_->getUserData()->offeredDraw_) movedPlayerData->gameManager_->gameResultHandler(true, false, "Agreement", "a");
        }
        else movedPlayerData->gameManager_->gameResultHandler(false, movedPlayerData->isWhite_ == false, "Resignation", "R");
    }
    // "req-" and "res-" comes to server from users
    else if(msgType == "req-rematch") {
        if(movedPlayerData->inGame_) return;

        // if other player exited
        if(!movedPlayerData->otherWS_) {
            ws->send("rn", uWS::OpCode::TEXT);
            return;
        }

        // if other player is already in a game
        if(movedPlayerData->otherWS_->getUserData()->inGame_) {
            ws->send("rg", uWS::OpCode::TEXT);
            return;
        }

        movedPlayerData->askedRematch_ = true;
        movedPlayerData->otherWS_->send("rematch");
    }
    else if(msgType == "res-rematch") {
        if(movedPlayerData->inGame_) return;

        // suspicious event or the rematch is already begun
        if(!movedPlayerData->otherWS_->getUserData()->askedRematch_) {
            return;
        }

        std::string response;
        ss >> response;

        // accepted Rematch
        if(response == "a") {
            rematch(movedPlayerData, movedPlayerData->otherWS_->getUserData());
            movedPlayerData->otherWS_->getUserData()->askedRematch_ = false;
            movedPlayerData->askedRematch_ = false;
        }
        else {
            movedPlayerData->otherWS_->send("rr");
        }

        // reset rematch status
        movedPlayerData->otherWS_->getUserData()->askedRematch_ = false;
    }
    else if(msgType == "new-game") {
        if(movedPlayerData->inGame_) return;

        movedPlayerData->inGame_ = true;

        if(regWaitingPlayer) matchPlayer(movedPlayerData, false);
        else regWaitingPlayer = movedPlayerData;
    }
}


void gameWSOpenHandler(uWS::WebSocket<true, true, PlayerData> * ws) {
    auto * newData = ws->getUserData();

    // if a reconnection is happening
    if(newData->inGame_) {
        if(newData->otherWS_) {
            newData->otherWS_->getUserData()->ws_ = ws;
        }
        return;
    }

    PlayerData** waitingPlayer = newData->name_ == "" ? &randWaitingPlayer : &regWaitingPlayer;

    if(*waitingPlayer) {
        matchPlayer(newData, false);
    }
    else {
        *waitingPlayer = newData;
    }
}


void randGameMoveHandler(PlayerData* movedPlayerData, std::string move) {
    auto & chess = movedPlayerData->chess_;
    
    try {
        chess->makeMove(move);
    } catch(std::exception& e) {
        std::cout << "Invalid Move Received: " << e.what() << std::endl;
        return;
    }

    json resultJson;
    resultJson["type"] = "result";

    // if opponent is checkmated
    if(chess->isCheckMate(!movedPlayerData->isWhite_)) {
        movedPlayerData->gameManager_->randGameResultHandler(false, movedPlayerData->isWhite_, "Checkmate");
    }
    // game is drawn
    else {
        std::string reason;

        if(chess->isStalemate()) reason = "Stalemate";
        else if(chess->isDrawByInsufficientMaterial()) reason = "Insufficient Material";
        else if(chess->isDrawByRepitition()) reason = "Repitition";
        else if(chess->isDrawBy50MoveRule()) reason = "50 Half Moves";

        movedPlayerData->gameManager_->randGameResultHandler(true, false, reason);
    }
}

void randGameWSMessageHandler(uWS::WebSocket<true, true, PlayerData>* ws, std::string_view msg, uWS::OpCode code) {
    auto* movedPlayerData = ws->getUserData();

    std::stringstream ss{std::string(msg)};
    std::string msgType;
    ss >> msgType;

    if(msgType == "move") {
        if(!movedPlayerData->inGame_) return;

        auto & chess = ws->getUserData()->chess_;

        std::string move;
        ss >> move;

        randGameMoveHandler(movedPlayerData, move);
    }
    else if(msgType == "chat") {
        if(!movedPlayerData->otherWS_ || movedPlayerData->otherWS_->getUserData()->inGame_) return;

        ws->getUserData()->otherWS_->send(msg);
    }
    else if(msgType == "draw") {
        if(!movedPlayerData->inGame_) return;

        movedPlayerData->offeredDraw_ = !movedPlayerData->offeredDraw_;
        ws->getUserData()->otherWS_->send(msg);
    }
    else if(msgType == "result") {
        if(!movedPlayerData->inGame_) return;

        std::string result;
        ss >> result;

        json resultJson;
        resultJson["type"] = "result";

        if(result == "d") {
            if(movedPlayerData->otherWS_->getUserData()->offeredDraw_) {
                resultJson["result"] = "draw";
                resultJson["reason"] = "Agreement";

                movedPlayerData->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
                movedPlayerData->otherWS_->send(resultJson.dump(), uWS::OpCode::TEXT);
            }
        }
        else {
            resultJson["result"] = "lose";
            resultJson["reason"] = "Resignation";

            movedPlayerData->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);

            resultJson["result"] = "win";
            movedPlayerData->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
        }
    }
    // "req-" and "res-" comes to server from users
    else if(msgType == "req-rematch") {
        if(movedPlayerData->inGame_) return;

        // if other player exited
        if(!movedPlayerData->otherWS_) {
            ws->send("rn", uWS::OpCode::TEXT);
            return;
        }

        // if other player is already in a game
        if(movedPlayerData->otherWS_->getUserData()->inGame_) {
            ws->send("rg", uWS::OpCode::TEXT);
            return;
        }

        movedPlayerData->askedRematch_ = true;
        movedPlayerData->otherWS_->send("rematch");
    }
    else if(msgType == "res-rematch") {
        if(movedPlayerData->inGame_) return;

        // suspicious event or the rematch is already begun
        if(!movedPlayerData->otherWS_->getUserData()->askedRematch_) {
            return;
        }

        std::string response;
        ss >> response;

        // accepted Rematch
        if(response == "a") {
            rematch(movedPlayerData, movedPlayerData->otherWS_->getUserData());
            movedPlayerData->otherWS_->getUserData()->askedRematch_ = false;
            movedPlayerData->askedRematch_ = false;
        }
        else {
            movedPlayerData->otherWS_->send("rr");
        }

        // reset rematch status
        movedPlayerData->otherWS_->getUserData()->askedRematch_ = false;
    }
    else if(msgType == "new-game") {
        if(movedPlayerData->inGame_) return;

        movedPlayerData->inGame_ = true;

        if(regWaitingPlayer) matchPlayer(movedPlayerData, true);
        else regWaitingPlayer = movedPlayerData;
    }
}

void gameWSCloseHandler(uWS::WebSocket<true, true, PlayerData> * ws, int code, std::string_view msg) {
    if(!ws->getUserData()->inGame_) return;

    // store the player data
    PlayerData* playerData = new PlayerData(std::move(*ws->getUserData()));
    playerData->ws_ = playerData->otherWS_->getUserData()->otherWS_ = nullptr;

    closedConnections.insert({playerData->id_, playerData});

    playerData->startAbandonTimer();

    std::cout << "Connection Closed: " << playerData->name_ << '\n';
} 


void handleSignupOrLogin(uWS::HttpResponse<true>* res, std::string_view body, bool isSignup) {
    // Expecting body in format: username=<username>&password=<password>
    std::string bodyStr(body);
    std::unordered_map<std::string, std::string> form;
    std::istringstream ss(bodyStr);
    std::string item;
    while (std::getline(ss, item, '&')) {
        auto pos = item.find('=');
        if (pos != std::string::npos) {
            form[item.substr(0, pos)] = item.substr(pos + 1);
        }
    }

    if(form.count("username") == 0 || form.count("password") == 0) {
        std::cout << "No credentials in body for signup or login\n";
        res->writeStatus("404 Bad Request")->end("No credentials available.");
    }

    std::string username = form["username"];
    std::string password = form["password"];

    tPool.submit([res, username, password, isSignup]() {
        try {
            auto connHandle = cPool.acquire();
            pqxx::work txn(*connHandle->get());

            if (isSignup) {
                pqxx::zview query("SELECT username FROM player WHERE username = $1");
                pqxx::params params(username);
                pqxx::result r = txn.exec(query, params);

                if (!r.empty()) {
                    res->writeStatus("409 Conflict")->end("Username already exists.");
                    return;
                }


                pqxx::result tranResult = txn.exec(pqxx::zview("INSERT INTO player (username, password) VALUES ($1, $2) RETURNING id"), pqxx::params(username, password));
                txn.commit();
                
                int userID = tranResult[0][0].as<int>();

                std::string token = createJWT(userID, username);

                json userJson;
                userJson["token"] = token;
                userJson["name"] = username;
                userJson["id"] = userID;

                res->writeHeader("Content-Type", "json");
                res->write(userJson.dump());
            } else {
                pqxx::zview query = "SELECT (id, password) FROM player WHERE username = $1";
                pqxx::params params(username);
                
                pqxx::result r = txn.exec(query, params);
                if (r.empty() || r[0][1].as<std::string>() != password) {
                    res->writeStatus("401 Unauthorized")->end("Invalid credentials.");
                    return;
                }

                int userID = r[0][0].as<int>();

                std::string token = createJWT(userID, username);

                json userJson;
                userJson["token"] = token;
                userJson["name"] = username;
                userJson["id"] = userID;

                res->writeHeader("Content-Type", "json");
                res->write(userJson.dump());
            }
        } catch (const std::exception& e) {
            res->writeStatus("500 Internal Server Error")->end("Database error.");
        }
    });
}

void gameHistoryHandler(uWS::HttpResponse<true>* res, int userID, int batchNumber, int numGames) {
    tPool.submit([=]() {
        auto connHandle = cPool.acquire();
        pqxx::work txn{*connHandle->get()};

        pqxx::result allGames = txn.exec(pqxx::zview("SELECT game_id from user_to_table WHERE user_id = $1 ORDER BY id DESC OFFSET $2 LIMIT $3"), pqxx::params(userID, batchNumber*20, numGames));

        json gamesJson;
        gamesJson["games"] = json::array();

        std::string gamesList = "";

        for(auto row : allGames) {
            std::string sGameID = row[0].as<std::string>();

            gamesList += sGameID;
            gamesList.push_back(',');
        }

        gamesList.pop_back();
        std::cout << "Games: " << gamesList << '\n';

        pqxx::result gamesResult = txn.exec(pqxx::zview("SELECT white_name, black_name, result, reason, created_at FROM game WHERE id in (" + gamesList + ")"));

        json gameJson;
        for(auto game : gamesResult) {
            gameJson["white"] = game[0].as<std::string>();
            gameJson["black"] = game[1].as<std::string>();
            gameJson["result"] = game[2].as<std::string>();
            gameJson["reason"] = game[3].as<std::string>();
            gameJson["created_at"] = game[4].as<std::string>();

            gamesJson["games"].push_back(gameJson);
        }

        res->writeHeader("Content-Type", "json");
        res->write(gamesJson.dump());
    });
}


void getLiveGamesHandler(uWS::HttpResponse<true>* res, int numGames) {
    json liveGamesJson;
    liveGamesJson["games"] = json::array();

    json gameJson;
    for(auto [gameID, gameManager] : liveGames) {
        gameJson["id"] = gameID;
        gameJson["white"] = gameManager->whiteData_->name_;
        gameJson["black"] = gameManager->blackData_->name_;

        liveGamesJson["games"].push_back(gameJson);

        --numGames;
        if(numGames == 0) break;
    }

    res->writeHeader("Content-Type", "json");
    res->write(liveGamesJson.dump());
}

void getPlayerProfile(uWS::HttpResponse<true>* res, std::string username) {
    if(username.length() == 0) {
        res->write("Player doesn't exist. Please check the username.");
        return;
    }   

    tPool.submit([=]() {
        auto connHandle = cPool.acquire();
        pqxx::work txn{*connHandle->get()};

        pqxx::result r = txn.exec(pqxx::zview("SELECT id FROM player WHERE user_name = $1"), pqxx::params(username));

        if(r.size() == 0) {
            res->write("Player doesn't exist. Please check the username.");
            return;
        }

        int userID = r[0][0].as<int>();

        pqxx::result allGames = txn.exec(pqxx::zview("SELECT game_id, result from user_to_table WHERE user_id = $1 ORDER BY id DESC OFFSET $2 LIMIT $3"), pqxx::params(userID, 0, 20));
        
        std::string gamesList = "";
        int numWon = 0, numLost = 0;

        json profileJson;

        profileJson["name"] = username;
        profileJson["id"] = userID;

        for(auto row : allGames) {
            std::string sGameID = row[0].as<std::string>();

            gamesList += sGameID;
            gamesList.push_back(',');

            if(row[1].as<std::string>() == "won") numWon++;
            else if(row[1].as<std::string>() == "lost") numLost++;
        }
        gamesList.pop_back();


        profileJson["games"] = json::array();
        profileJson["total"] = allGames.size();
        profileJson["won"] = numWon;
        profileJson["lost"] = numLost;

        std::cout << "Games: " << gamesList << '\n';

        pqxx::result gamesResult = txn.exec(pqxx::zview("SELECT white_name, black_name, result, reason, created_at FROM game WHERE id in (" + gamesList + ")"));

        json gameJson;
        for(auto game : gamesResult) {
            gameJson["white"] = game[0].as<std::string>();
            gameJson["black"] = game[1].as<std::string>();
            gameJson["result"] = game[2].as<std::string>();
            gameJson["reason"] = game[3].as<std::string>();
            gameJson["created"] = game[4].as<std::string>();

            profileJson["games"].push_back(gameJson);
        }

        res->writeHeader("Content-Type", "json");
        res->write(profileJson.dump());
    });
}

std::string getLiveGameOfUser(int userID) {
    auto it = playersInGame.find(userID);

    if(it == playersInGame.end()) {
        return "-";
    }

    return it->second->sGameID_;
}


void getGameHandler(uWS::HttpResponse<true>* res, int gameID, int reqUserID) {
    auto liveGamesIT = liveGames.find(gameID);
        
    // if it is a live game
    if(liveGamesIT != liveGames.end()) {
        auto * gameMngr = liveGamesIT->second;

        // if the requested player is already playing the game
        if(reqUserID != -1 && (gameMngr->whiteData_->id_ == reqUserID || gameMngr->blackData_->id_ == reqUserID)) {
            res->write("You are already playing in this game. If you wanted to play in a new window, please close the previous session and try again.");
            return;
        }

        json liveJson;
        liveJson["live"] = true;
        liveJson["socket"] = "watch?topic=" + std::to_string(gameID);

        res->write(liveJson.dump());
    }
    // not a live game
    else {
        tPool.submit([res, gameID]() {
            auto connHandle = cPool.acquire();
            pqxx::work txn{*connHandle->get()};

            try {
                pqxx::result gameResult = txn.exec(pqxx::zview("SELECT white_name, black_name, move_history, result, reason, created_at FROM game WHERE id = $1"), pqxx::params(gameID));

                if(gameResult.size() == 0) {
                    res->write("Invalid game id");
                    return;
                }

                json gameJson;

                gameJson["id"] = gameID;
                gameJson["white"] = gameResult[0][0].as<std::string>();
                gameJson["black"] = gameResult[0][1].as<std::string>();
                gameJson["moves"] = gameResult[0][2].as<std::string>();
                gameJson["result"] = gameResult[0][3].as<std::string>();
                gameJson["reason"] = gameResult[0][4].as<std::string>();
                gameJson["created_at"] = gameResult[0][5].as<std::string>();

                res->writeHeader("Content-Type", "json");
                res->write(gameJson.dump());
            }
            catch (std::exception& e) {
                std::cout << "DB Error in fetching game: " << e.what() << '\n';
                res->writeStatus("500 Internal Server Error")->write("Something went wrong. Please try again later.");
            }
        });
    }
}


void watchWSUpgradeHandler(uWS::HttpResponse<true> * res, uWS::HttpRequest * req, struct us_socket_context_t * context) {
    bool connectionAborted = false;

    res->onAborted([&connectionAborted]() {
        connectionAborted = true;
    });

    GameManager* gamePointer;

    // get the topic
    std::string topic(req->getQuery("topic"));
    if(topic.length() == 0 || !std::all_of(topic.begin(), topic.end(), ::isdigit)) {
        auto it = liveGames.find(stoi(topic));

        if(it == liveGames.end()) {
            res->write("Invalid topic received");
            return;
        }
        else gamePointer = it->second;
    }

    /* If we have this header set, it's a websocket */
    std::string_view secWebSocketKey = req->getHeader("sec-websocket-key");
    if (secWebSocketKey.length() == 24) {
        std::string_view secWebSocketProtocol = req->getHeader("sec-websocket-protocol");
        std::string_view secWebSocketExtensions = req->getHeader("sec-websocket-extensions");

        if(!connectionAborted) 
            res->template upgrade<GameManagerPointer>(GameManagerPointer(gamePointer), secWebSocketKey, secWebSocketProtocol, secWebSocketExtensions, context);
        else std::cout << "Connection Aborted before upgrade\n";
    }
    else {
        std::cout << "Couldn't create web socket connection. Invalid Web Socket Key.\n";
        /* Tell the router that we did not handle this request */
        req->setYield(true);
    }
}

