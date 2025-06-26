#include "App.h"
#include "jwt.h"
#include "nlohmann/json.hpp"


#include "Data.h"
#include "ThreadPool.h"
#include "ConnectionPool.h"

#include <string>

using json = nlohmann::json;
PlayerData* randWaitingPlayer = nullptr, *regWaitingPlayer = nullptr;

const std::string connStr = "host=localhost port=5432 dbname=chess user=postgres password=yourpassword";
PostgresConnectionPool cPool(10);
ThreadPool tPool(2);

// JWT secret key
const std::string jwtSecret = "your_secret_key";

std::unordered_map<std::string, PlayerData*> closedConnections;
std::unordered_map<int, std::pair<PlayerData*, PlayerData*>> liveGames;
std::unordered_map<int, PlayerData*> playersInGame;


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

    if(!waitingPlayer->moveTimer_) waitingPlayer->moveTimer_ = std::make_unique<MoveTimer>(300, uv_loop, waitingPlayer);
    if(!newPlayerData->moveTimer_) newPlayerData->moveTimer_ = std::make_unique<MoveTimer>(300, uv_loop, newPlayerData);

    waitingPlayer->inGame_ = newPlayerData->inGame_ = true;

    tPool.sumbit([waitingPlayerRef, waitingPlayer, newPlayerData](){
        auto connHandle = cPool.acquire();
        pqxx::work txn(*connHandle->get());

        pqxx::result txnResult = txn.exec(
            pqxx::zview("INSERT INTO game (white_id, black_id, move_history, result, reason) "
            "VALUES ($1, $2, $3, $4, $5) RETURNING id"),
            pqxx::params(whiteName, blackName, move_history, result, reason)
        );

        if(txnResult.empty()) {
            std::cerr << "Couldn't fetch new game_id after insertion." << std::endl;
            return;
        }

        txn.commit();

        json toNewPlayer;
        toNewPlayer["type"] = "start-game";
        toNewPlayer["white"] = newPlayerData->isWhite;

        json toWaitingPlayer;
        toWaitingPlayer["type"] = "start-game";
        toWaitingPlayer["white"] = waitingPlayer->isWhite;

        std::cout << "player's matched" << std::endl;

        newPlayerData->ws->send(toNewPlayer.dump(), uWS::OpCode::TEXT);
        waitingPlayer->ws->send(toWaitingPlayer.dump(), uWS::OpCode::TEXT);

        // To-Do: should i call this after starting player timers?
        newPlayerData->createSyncTimer();
        waitingPlayer->createSyncTimer(newPlayerData->syncTimer);

        if(waitingPlayer->isWhite_) {
            waitingPlayer->gameManager_ = newPlayerData->gameManager_ = std::make_shared<GameManager>(txnResult[0][0].as<int>(), waitingPlayer, newPlayerData);
            waitingPlayer->timerData->startTimer();
        }
        else {
            waitingPlayer->gameManager_ = newPlayerData->gameManager_ = std::make_shared<GameManager>(txnResult[0][0].as<int>(), newPlayerData, waitingPlayer);
            newPlayerData->timerData->startTimer();
        }

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
        id = decoded.get_payload_claim("id").as_string();

    } catch (const std::exception& e) {
        std::cout << "Invalid token: " << e.what() << std::endl;
    }

    return {id, username};
}


void gameWSUpgradeHandler(uWS::HttpResponse<true> * res, uWS::HttpRequest * req, struct us_socket_context_t * context) {
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

        /* Safari 15 hack */
        if (hasBrokenCompression(req->getHeader("user-agent"))) {
            secWebSocketExtensions = "";
        }

        auto it = closedConnections.find(id);

        if(it != closedConnections.end()) {
            auto* playerData = it->second;
            closedConnections.erase(it);

            // stop the running abandon timer
            playerData->stopAbandonTimer();

            res->upgrade(std::move(*playerData), secWebSocketKey, secWebSocketProtocol, secWebSocketExtensions, context);
        }
        else if(playersInGame.find(id) != playersInGame.end()) {
            // To-Do: should i refuse the connection or let it play with this new connection?
            return;
        }
        else {
            PlayerData playerData;
            playerData.name_ = username;
            playerData.id_ = id;
            res->upgrade(std::move(playerData), secWebSocketKey, secWebSocketProtocol, secWebSocketExtensions, context);
        }

    } else {
        std::cout << "Couldn't create web socket connection. Invalid Web Socket Key.\n";
        /* Tell the router that we did not handle this request */
        req->setYield(true);
    }
}

void gameMoveHandler(PlayerData* movedPlayerData, std::string move) {
    auto & chess = movedPayerData->chess;

    try {
        chess->makeMove(move);
    } catch(std::exception& e) {
        std::cout << "Invalid Move Received: " << e.what() << std::endl;
        return;
    }

    if(chess->isGameOver()) {
        bool isDraw = false, whiteWon = false;
        std::string reason;
        char dbReason;
        
        // if opponent is checkmated
        if(chess->isCheckmate(!movedPlayerData->isWhite_)) {
            reason = "Checkmate";
            dbReason = 'c';
            whiteWon = movedPlayerData->isWhite_ == true;
        }
        // game is drawn
        else {
            isDraw = true;

            if(chess->isStalemate()) reason = "Stalemate", dbReason = 's';
            else if(chess->isDrawByInsufficientMaterial()) reason = "Insufficient Material", dbReason = 'i';
            else if(chess->isDrawByRepitition()) reason = "Repitition", dbReason = 'r';
            else if(chess->isDrawBy50MoveRule()) reason = "50 Half Moves", dbReason = 'f';
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

        movedPlayerData->gameManager_->gameResultHandler(isDraw, whiteWon, reason, dbReason, whiteData, blackData);
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

    tPool.sumbit([p1, p2](){
        auto connHandle = cPool.acquire();
        pqxx::work txn(*connHandle->get());

        pqxx::result txnResult = txn.exec(
            pqxx::zview("INSERT INTO game (white_id, black_id, move_history, result, reason) "
            "VALUES ($1, $2, $3, $4, $5) RETURNING id"),
            pqxx::params((p1->isWhite_ ? p1->name_ : p2->name_), (p2->isWhite_ ? p1->name_ : p2->name_), move_history, result, reason)
        );

        if(txnResult.empty()) {
            std::cerr << "Couldn't fetch new game_id after insertion." << std::endl;
            return;
        }

        txn.commit();

        json toP1;
        toP1["type"] = "start-game";
        toP1["white"] = p1->isWhite;

        json toP2;
        toP2["type"] = "start-game";
        toP2["white"] = p2->isWhite;

        std::cout << "player's re-matched" << std::endl;

        p1->ws->send(toP1.dump(), uWS::OpCode::TEXT);
        p2->ws->send(toP2.dump(), uWS::OpCode::TEXT);


        // It is assumed that all the relvant data of both players and game is reset

        if(p1->isWhite) {
            p1->gameManager_->whiteData_ = p1;
            p1->gameManager_->blackData_ = p2;
            p1->moveTimer->start();
        }
        else {
            p1->gameManager_->whiteData_ = p2;
            p1->gameManager_->blackData_ = p1;
            p2->moveTimer->start();
        }
    });

}


void gameWSMessageHandler(uWS::WebSocket<true, true, PlayerData>* ws, std::string_view msg, uWS::OpCode code) {
    auto* movedPlayerData = ws->getUserData();

    std::stringstream ss{std::string(msg)};
    std::string msgType;
    ss >> msgType;

    if(msgType == "move") {
        auto & chess = ws_->getUserData()->chess;

        std::string move;
        ss >> move;

        gameWSMoveHandler(movedPlayerData, move);
    }
    else if(msgType == "chat") {
        ws->getUserData()->otherWs_->send(msg);
    }
    else if(msgType == "draw") {
        movedPlayerData->offeredDraw_ = !movedPlayerData->offeredDraw_;
        ws->getUserData()->otherWs_->send(msg);
    }
    else if(msgType == "result") {
        std::string result;
        ss >> result;

        // To-Do: does the order of player data not matter?
        if(result == "d") {
            if(movedPlayerData->otherWs->getUserData()->offeredDraw_) gameResultHandler(true, false, "Agreement", 'a', movedPlayerData, movedPlayerData->otherWS_->getUserData());
        }
        else gameResultHandler(false, movedPlayerData->isWhite_ == false, "Resigned", "R", movedPlayerData, movedPlayerData->otherWS_->getUserData());
    }
    // "req-" and "res-" comes to server from users
    else if(msgType == "req-rematch") {
        // if other player exited
        if(!movedPlayerData->otheWS_) {
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
        // suspicious event or the rematch is already begun
        if(!movedPlayerData->otherWS_->geUserData()->askedRematch_) {
            return;
        }

        std::string response;
        ss >> response;

        // accepted Rematch
        if(response == "a") {
            rematch(movedPlayerData, movedPlayerData->otherWS_->getUserData());
            movedPlayerData->otherWS_->geUserData()->askedRematch_ = false;
            movedPlayerData->askedRematch_ = false;
        }
        else {
            movedPlayerData->other_WS_->send("rr");
        }

        // reset rematch status
        movedPlayerData->otherWS_->geUserData()->askedRematch_ = false;
    }
    else if(msgType == "new-game") {
        if(movedPlayerData->inGame_) return;

        movedPlayerData->inGame_ = true;

        if(regWaitingPlayer) matchPlayer(movedPlayerData);
        else regWaitingPlayer = movedPlayerData;
    }
}


void gameWSOpenHandler(uWS::WebSocket<true, true, PlayerData> * ws) {
    auto * newData = ws->getUserData();

    // if a reconnection is happening
    if(newData->inGame_) {
        if(newData->otherWS_) {
            newData->otherWs_->getUserData()->ws_ = ws;
        }
        return;
    }

    PlayerData** waitingPlayer = newData->name == "" ? &randWaitingPlayer : &regWaitingPlayer;

    if(*waitingPlayer) {
        matchPlayer(newData, false);
    }
    else {
        *waitingPlayer = newData;
    }
}


void randGameMoveHandler(PlayerData* movedPlayerData, std::string move) {
    auto & chess = movedPayerData->chess_;
    
    try {
        chess->makeMove(move);
    } catch(std::exception& e) {
        std::cout << "Invalid Move Received: " << e.what() << std::endl;
        return;
    }

    json resultJson;
    resultJson["type"] = "result";

    // if opponent is checkmated
    if(chess->isCheckmate(!movedPlayerData->isWhite_)) {
        movedPlayerData->gameManager_->randGameResultHandler(false, movedPlayerData->isWhite, "Checkmate");
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
        auto & chess = ws_->getUserData()->chess_;

        std::string move;
        ss >> move;

        randGameWSMoveHandler(movedPlayerData, move);
    }
    else if(msgType == "chat") {
        ws_->getUserData()->otherWs_->send(msg);
    }
    else if(msgType == "draw") {
        movedPlayerData->offeredDraw_ = !movedPlayerData->offeredDraw_;
        ws_->getUserData()->otherWs_->send(msg);
    }
    else if(msgType == "result") {
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
            resultJson["reason"] = "Resign";

            movedPlayerData->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);

            resultJson["result"] = "win";
            movedPlayerData->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
        }
    }
}

void gameWSCloseHandler(uWS::WebSocket<true, true, PlayerData> * ws, int code, std::string_view msg) {
    if(!ws->getUserData()->inGame_) return;

    // store the player data
    PlayerData* playerData = new PlayerData(std::move(*ws->getUserData()));
    playerData->ws_ = playerData->otherWS_->getUserData()->otherWS_ = nullptr;

    closedConnections.insert({playerData->id_, playerData});

    playerData->startAbandonTimer();

    std::cout << "Connection Closed: " << playerData->name << '\n';
} 