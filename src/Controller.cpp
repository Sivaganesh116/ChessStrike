#include "App.h"
#include "jwt.h"
#include "nlohmann/json.hpp"


#include "Data.h"
#include "ThreadPool.h"
#include "ConnectionPool.h"

#include <string>

using json = nlohmann::json;
PlayerData* waitingPlayer = nullptr, *regWaitingPlayer = nullptr;

const std::string connStr = "host=localhost port=5432 dbname=chess user=postgres password=yourpassword";
PostgresConnectionPool cPool(10);
ThreadPool tPool(2);


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

std::string getUserNameFromToken(std::string token) {
    std::string username = "";

    try {
        auto decoded = jwt::decode(token);

        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{jwtSecret})
            .with_issuer("chess-server");

        verifier.verify(decoded);  // Throws if verification fails

        std::string username = decoded.get_payload_claim("username").as_string();

        // res->end("Authenticated as: " + username);
    } catch (const std::exception& e) {
        std::cout << "Invalid token: " << e.what() << std::endl;
    }

    return username;
}

void gameResultDBHandler(int gameID, int whiteID, int blackID, char dbReason, bool isDraw, bool whiteWon) {
    char gameResult, whiteResult, blackResult;
    
    if(isDraw) {
        gameResult = whiteResult = blackResult = 'd';
    }
    else if(whiteWon) {
        gameResult = whiteResult = 'w';
        blackResult = "l";
    }
    else {
        gameResult = 'b';
        whiteResult = 'l';
        blackResult = 'w';
    }

    tPool.submit([gameID, whiteID, blackID, dbReason, gameResult, whiteResult, blackResult]() {
        auto connHandle = pool.acquire();
        pqxx::work txn(*connHandle->get());

        try {
            // update table "game"
            pqxx::result txnResult = txn.exec(
                pqxx::zview("UPDATE game SET reason = $1, result = $2 WHERE game_id = $3;"),
                pqxx::params(dbReason, gameResult, gameID)
            );

            // insert entry into "user_to_game"
            txn.exec(
                pqxx::zview("INSERT INTO user_to_game (user_id, game_id, result) "
                "VALUES ($1, $2, $3)"),
                pqxx::params(whiteID, gameID, whiteResult)
            );

            txn.exec(
                pqxx::zview("INSERT INTO user_to_game (user_id, game_id, result) "
                "VALUES ($1, $2, $3)"),
                pqxx::params(blackID, gameID, blackResult)
            );

            // To-Do: check for "error thrown: "in_doubt_error"
            txn.commit();
        } catch (const std::exception& e) {
            std::cerr << "Error handling db operations of game result of game id: " << gameID << ". " << e.what() << std::endl;
        }
    });
}

void gameResultHandler(bool isDraw, bool whiteWon, std::string_view reason, char dbReason, PlayerData* whiteData, PlayerData* blackData) {
    json resultJson;
    resultJson["type"] = "result";
    resultJson["reason"] = reason;

    if(isDraw) {
        resultJson["result"] = "draw";
        whiteData->ws->send(resultJson.dump(), uWS::OpCode::TEXT);
        blackData->ws->send(resultJson.dump(), uWS::OpCode::TEXT);
        return;
    }
    
    resultJson["result"] = "win";

    if(whiteWon) {
        whiteData->ws->send(resultJson.dump(), uWS::OpCode::TEXT);
        resultJson["result"] = "lose";
        blackData->ws->send(resultJson.dump(), uWS::OpCode::TEXT);
    }
    else {
        blackData->ws->send(resultJson.dump(), uWS::OpCode::TEXT);
        resultJson["result"] = "lose";
        whiteData->ws->send(resultJson.dump(), uWS::OpCode::TEXT);
    }

    gameResultDBHandler(whiteData->gameID_, whiteData->id_, blackData->id_, dbReason, isDraw, whiteWon);
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

    std::string username = getUserNameFromToken(token);

    if(username.length() == 0) {
        std::cout << "Not Authorized to create web socket connection.\n";
    }


    /* If we have this header set, it's a websocket */
    std::string_view secWebSocketKey = req->getHeader("sec-websocket-key");
    if (secWebSocketKey.length() == 24) {
        /* Default handler upgrades to WebSocket */
        std::string_view secWebSocketProtocol = req->getHeader("sec-websocket-protocol");
        std::string_view secWebSocketExtensions = req->getHeader("sec-websocket-extensions");

        /* Safari 15 hack */
        if (hasBrokenCompression(req->getHeader("user-agent"))) {
            secWebSocketExtensions = "";
        }

        PlayerData playerData;
        playerData->name = username;
        // To-Do: Player Data's id;

        res->upgrade(std::move(PlayerData), secWebSocketKey, secWebSocketProtocol, secWebSocketExtensions, context);
    } else {
        std::cout << "Couldn't create web socket connection. Invalid Web Socket Key.\n";
        /* Tell the router that we did not handle this request */
        req->setYield(true);
    }
}

void gameWSMoveHandler(PlayerData* movedPlayerData, std::string move) {
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

        gameResultHandler(isDraw, whiteWon, reason, dbReason, whiteData, blackData);
    }
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
}

void randGameWSMoveHandler(PlayerData* movedPlayerData, std::string move) {
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
        resultJson["reason"] = "Checkmate";
        resultJson["result"] = "win";

        movedPlayerData->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);

        resultJson["result"] = "lose";

        movedPlayerData->otherWs_->send(resultJson.dump(), uWS::OpCode::TEXT);
    }
    // game is drawn
    else {
        resultJson["result"] = "draw";
        if(chess->isStalemate()) resultJson["reason"] = "Stalemate";
        else if(chess->isDrawByInsufficientMaterial()) resultJson["reason"] = "Insufficient Material";
        else if(chess->isDrawByRepitition()) resultJson["reason"] = "Repitition";
        else if(chess->isDrawBy50MoveRule()) resultJson["reason"] = "50 Half Moves";

        movedPlayerData->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
        movedPlayerData->otherWs_->send(resultJson.dump(), uWS::OpCode::TEXT);
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