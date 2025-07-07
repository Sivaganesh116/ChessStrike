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

const std::string connStr = "host=localhost port=5432 dbname=chessstrike user=postgres password=root";
PostgresConnectionPool cPool(connStr, 10);
ThreadPool tPool(2);

// JWT secret key
const std::string jwtSecret = "your_secret_key";

std::unordered_map<int, PlayerData*> closedConnections;
std::unordered_map<int, GameManager*> liveGames;
std::unordered_map<int, GameManager*> playersInGame;

extern std::shared_ptr<uv_loop_t> uv_loop;


// Helper function to determine the content type from a file extension
std::string getMimeType(const std::string& path) {
    // A simple map for common web file types
    static const std::map<std::string, std::string> mime_map = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".gif", "image/gif"}
    };
    
    size_t pos = path.rfind('.');
    if (pos != std::string::npos) {
        std::string ext = path.substr(pos);
        if (mime_map.count(ext)) {
            return mime_map.at(ext);
        }
    }
    return "application/octet-stream"; // Default binary type
}


void serveFile(std::string filePath, uWS::HttpResponse<true>* res) {
    // Try to open and read the file
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        // File not found, send a 404 response
        res->writeStatus("404 Not Found");
        res->end("File not found");
        return;
    }

    // Read the entire file into a string stream
    std::stringstream buffer;
    buffer << file.rdbuf();

    // Send the response with the correct MIME type
    res->writeHeader("Content-Type", getMimeType(filePath));
    res->write(buffer.str());
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
        waitingPlayer->isMyTurn_ = true;

        if(newPlayerData->name_ == "") newPlayerData->name_ = "black";
        newPlayerData->isWhite_ = false;
    }
    else {
        if(waitingPlayer->name_ == "") waitingPlayer->name_ = "black";
        waitingPlayer->isWhite_ = false;

        if(newPlayerData->name_ == "") newPlayerData->name_ = "white";
        newPlayerData->isWhite_ = true;
        newPlayerData->isMyTurn_ = true;
    }

    if(waitingPlayer->chess_) newPlayerData->chess_ = waitingPlayer->chess_;
    else if(newPlayerData->chess_) waitingPlayer->chess_ = newPlayerData->chess_;
    else waitingPlayer->chess_ = newPlayerData->chess_ = std::make_shared<LC::LegalChess>();

    waitingPlayer->otherWS_ = newPlayerData->ws_;
    newPlayerData->otherWS_ = waitingPlayer->ws_;

    if(!waitingPlayer->moveTimer_) waitingPlayer->moveTimer_ = std::make_unique<MoveTimer>(300, uv_loop.get(), waitingPlayer);
    if(!newPlayerData->moveTimer_) newPlayerData->moveTimer_ = std::make_unique<MoveTimer>(300, uv_loop.get(), newPlayerData);

    waitingPlayer->inGame_ = newPlayerData->inGame_ = true;

    // reset the waiting player
    *waitingPlayerRef = nullptr;

    tPool.submit([waitingPlayer, newPlayerData, randGame](){
        int gameID = -1;

        if(!randGame) {
            auto connHandle = cPool.acquire();
            pqxx::work txn(*connHandle->get());

            pqxx::result txnResult = txn.exec(
                pqxx::zview("INSERT INTO game (white_name, black_name) "
                "VALUES ($1, $2) RETURNING id"),
                pqxx::params((waitingPlayer->isWhite_ ? waitingPlayer->name_ : newPlayerData->name_), (!waitingPlayer->isWhite_ ? waitingPlayer->name_ : newPlayerData->name_))
            );

            if(txnResult.empty()) {
                std::cerr << "Couldn't fetch new game_id after insertion." << std::endl;
                return;
            }

            gameID = txnResult[0][0].as<int>();

            txn.commit();
        }

        waitingPlayer->gameManager_ = newPlayerData->gameManager_ = std::make_shared<GameManager>(gameID, waitingPlayer->isWhite_ ? waitingPlayer : newPlayerData, newPlayerData->isWhite_ ? waitingPlayer : newPlayerData);

        std::stringstream toNewPlayer;
        toNewPlayer << "start-game" << " " << newPlayerData->gameManager_->sGameID_ << " " << (newPlayerData->isWhite_ ? "w" : "b") << " " << waitingPlayer->name_ << " " << "300"; 

        std::stringstream toWaitingPlayer;
        toWaitingPlayer << "start-game" << " " << waitingPlayer->gameManager_->sGameID_ << " " << (waitingPlayer->isWhite_ ? "w" : "b") << " " << newPlayerData->name_ << " " << "300"; 

        newPlayerData->ws_->send(toNewPlayer.str(), uWS::OpCode::TEXT);
        waitingPlayer->ws_->send(toWaitingPlayer.str(), uWS::OpCode::TEXT);

        newPlayerData->ws_->subscribe(std::to_string(gameID));
        waitingPlayer->ws_->subscribe(std::to_string(gameID));

        playersInGame.insert({waitingPlayer->id_, waitingPlayer->gameManager_.get()});
        playersInGame.insert({newPlayerData->id_, newPlayerData->gameManager_.get()});

        liveGames.insert({gameID, waitingPlayer->gameManager_.get()});

        if(waitingPlayer->isWhite_) {
            waitingPlayer->moveTimer_->start();
        }
        else {
            newPlayerData->moveTimer_->start();
        }

        // we are using a per move time sync, no need for a repeat timer to send time updates for now

        // waitingPlayer->gameManager_->startSyncTimer();
    });
}


std::unordered_map<std::string, std::string> parseCookies(std::string_view cookieHeader) {
    std::unordered_map<std::string, std::string> cookies;
    size_t pos = 0;

    while (pos < cookieHeader.size()) {
        // Find '=' separating key and value
        size_t eq = cookieHeader.find('=', pos);
        if (eq == std::string_view::npos) break;

        // Find end of this cookie (next ';' or end of string)
        size_t end = cookieHeader.find(';', eq);
        if (end == std::string_view::npos) end = cookieHeader.size();

        // Extract and trim key and value
        std::string key = std::string(cookieHeader.substr(pos, eq - pos));
        std::string value = std::string(cookieHeader.substr(eq + 1, end - eq - 1));

        // trim whitespace from key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        cookies[key] = value;

        // Move to next cookie, skip semicolon if present
        pos = (end < cookieHeader.size()) ? end + 1 : cookieHeader.size();

        // Skip any whitespace
        while (pos < cookieHeader.size() && std::isspace(cookieHeader[pos])) ++pos;
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
            .with_issuer("chess-strike");

        verifier.verify(decoded);  // Throws if verification fails

        username = decoded.get_payload_claim("username").as_string();
        id = stoi(decoded.get_payload_claim("id").as_string());

    } catch (const std::exception& e) {
        std::cout << "Invalid token: " << e.what() << std::endl;
    }

    return {id, username};
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
        return;
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
            res->writeHeader("Content-Type", "text/plain")->end("You are already playing in the requested game. Please switch back to the running game's tab.");
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
        ws->send("move false ", uWS::OpCode::TEXT);
        return;
    }

    movedPlayerData->moveTimer_->stop();
    auto* otherData = (movedPlayerData->gameManager_->whiteData_ == movedPlayerData ? movedPlayerData->gameManager_->blackData_ : movedPlayerData->gameManager_->whiteData_);

    movedPlayerData->isMyTurn_ = false;
    otherData->isMyTurn_ = true;

    auto [wsec, wnsec] = movedPlayerData->gameManager_->whiteData_->moveTimer_->getRemainingTime();
    auto [bsec, bnsec] = movedPlayerData->gameManager_->blackData_->moveTimer_->getRemainingTime();

    std::string swsec = std::to_string(wsec), swnsec = std::to_string(wnsec), sbsec = std::to_string(bsec), sbnsec = std::to_string(bnsec);

    if(otherData->ws_) otherData->ws_->send("newmove " + move + " " + swsec + " " + swnsec + " " + sbsec + " " + sbnsec, uWS::OpCode::TEXT);

    otherData->moveTimer_->start();

    ws->send("move true " + swsec + " " + swnsec + " " + sbsec + " " + sbnsec, uWS::OpCode::TEXT);

    json publishJson;
    publishJson["type"] = "newmove";
    publishJson["move"] = move;
    publishJson["whiteSec"] = std::to_string(wsec);
    publishJson["whiteNanoSec"] = std::to_string(wnsec);
    publishJson["blackSec"] = std::to_string(bsec);
    publishJson["blackNanoSec"] = std::to_string(bnsec);
    

    ws->publish(movedPlayerData->gameManager_->sGameID_, publishJson.dump(), uWS::OpCode::TEXT);

    movedPlayerData->gameManager_->timeStamps_ += swsec + "-" + sbsec + " ";

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
        p1->isMyTurn_ = true;
        p2->isWhite_ = false;
    }
    else {
        p1->isWhite_ = false;
        p2->isWhite_ = true;
        p2->isMyTurn_ = true;
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

        std::string sNewGameID = txnResult[0][0].as<std::string>();


        std::stringstream toP1;
        toP1 << "start-rematch" << " " << sNewGameID << " " << (p1->isWhite_ ? "w" : "b") << " " << "300" << " " << "300";

        std::stringstream toP2;
        toP2 << "start-rematch" << " " << sNewGameID << " " << (p2->isWhite_ ? "w" : "b") << " " << "300" << " " << "300";

        std::cout << "player's re-matched" << std::endl;

        p1->ws_->send(toP1.str(), uWS::OpCode::TEXT);
        p2->ws_->send(toP2.str(), uWS::OpCode::TEXT);

        // It is assumed that all the relvant data of both players and game is reset

        // publish to old topic about rematch to subscribers/watchers
        json rematchJson;
        rematchJson["type"] = "rematch";
        rematchJson["newGameID"] = sNewGameID;
        p1->ws_->publish(p1->gameManager_->sGameID_, rematchJson.dump(), uWS::OpCode::TEXT);

        p1->gameManager_->gameID_ = stoi(sNewGameID);
        p1->gameManager_->sGameID_ = sNewGameID;

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

        liveGames.insert({stoi(sNewGameID), p1->gameManager_.get()});
        
        // we are using a per move time sync, no need for a repeat timer to send time updates for now

        // p1->gameManager_->startSyncTimer();
    });
}


void gameWSMessageHandler(uWS::WebSocket<true, true, PlayerData>* ws, std::string_view msg, uWS::OpCode code) {
    auto* movedPlayerData = ws->getUserData();
    auto * otherData = (movedPlayerData->gameManager_->whiteData_ == movedPlayerData ? movedPlayerData->gameManager_->blackData_ : movedPlayerData->gameManager_->whiteData_);

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
        if(!movedPlayerData->otherWS_) return;

        movedPlayerData->otherWS_->send(msg, uWS::OpCode::TEXT);
    }
    else if(msgType == "req-draw") {
        if(!movedPlayerData->inGame_) return;

        movedPlayerData->offeredDraw_ = !movedPlayerData->offeredDraw_;
        if(movedPlayerData->otherWS_) movedPlayerData->otherWS_->send("draw", uWS::OpCode::TEXT);
    }
    else if(msgType == "res-draw") {
        if(!otherData->offeredDraw_) {
            std::cout << "Fishy user accepted draw when other player didn't offer one\n";
            return;
        }

        std::string drawResponse;
        ss >> drawResponse;

        if(drawResponse == "a") movedPlayerData->gameManager_->gameResultHandler(true, false, "Agreement", "a");
        else if(otherData->ws_) {
            otherData->ws_->send("dr", uWS::OpCode::TEXT);
            otherData->offeredDraw_ = false;
        }
    }
    else if(msgType == "resign") {
        if(!movedPlayerData->inGame_) return;

        movedPlayerData->gameManager_->gameResultHandler(false, movedPlayerData->isWhite_ == false, "Resignation", "R");
    }
    else if(msgType == "abort") {
        if(!movedPlayerData->inGame_ || movedPlayerData->chess_->getMoveHistory().length() > 11) return;

        std::string sendAbort = "result a a " + std::to_string(movedPlayerData->gameManager_->whiteScore_) + " " + std::to_string(movedPlayerData->gameManager_->blackScore_);
        ws->send(sendAbort, uWS::OpCode::TEXT);
        if(movedPlayerData->otherWS_) movedPlayerData->otherWS_->send(sendAbort, uWS::OpCode::TEXT);
        movedPlayerData->gameManager_->resetData();
    }
    // "req-" and "res-" comes to server from users
    else if(msgType == "req-rematch") {
        if(movedPlayerData->inGame_) return;

        // if other player exited or in other game
        if(!otherData || otherData->inGame_) {
            ws->send("rn", uWS::OpCode::TEXT);
            return;
        }

        movedPlayerData->askedRematch_ = true;
        movedPlayerData->otherWS_->send("rematch", uWS::OpCode::TEXT);
    }
    else if(msgType == "res-rematch") {
        if(movedPlayerData->inGame_) return;

        // suspicious event or the rematch is already begun
        if(!otherData->askedRematch_) {
            return;
        }

        if(!otherData) ws->send("rn", uWS::TEXT);

        std::string response;
        ss >> response;

        // accepted Rematch
        if(response == "a") {
            otherData->askedRematch_ = false;
            movedPlayerData->askedRematch_ = false;
            rematch(movedPlayerData, movedPlayerData->otherWS_->getUserData());
        }
        else {
            movedPlayerData->otherWS_->send("rr", uWS::OpCode::TEXT);
            // reset rematch status
            otherData->askedRematch_ = false;
        }
    }
    else if(msgType == "new-game") {
        if(movedPlayerData->inGame_) return;

        movedPlayerData->inGame_ = true;
        
        // player is starting a new game, sever the connection with the previous player
        if(otherData) otherData->otherWS_ = nullptr;

        if(regWaitingPlayer) matchPlayer(movedPlayerData, false);
        else regWaitingPlayer = movedPlayerData;
    }
}


void gameWSOpenHandler(uWS::WebSocket<true, true, PlayerData> * ws) {
    auto * newData = ws->getUserData();

    // if a reconnection is happening
    if(newData->inGame_) {
        newData->ws_ = ws;

        // change the player data pointer in game manager, as it would be pointing to a dangling after std::move
        if(newData->isWhite_) {
            newData->gameManager_->whiteData_ = newData;
            newData->gameManager_->blackData_->otherWS_ = ws;
        }
        else {
            newData->gameManager_->blackData_ = newData;
            newData->gameManager_->whiteData_->otherWS_ = ws;
        }

        auto [wsec, wnsec] = newData->gameManager_->whiteData_->moveTimer_->getRemainingTime();
        auto [bsec, bnsec] = newData->gameManager_->blackData_->moveTimer_->getRemainingTime(); 

        std::stringstream reconnectData;
        reconnectData << "reconnection" << " " << newData->gameManager_->sGameID_ << " " << (newData->isWhite_ ? "w" : "b") << " " << (newData->isWhite_ ? newData->gameManager_->blackData_->name_ : newData->gameManager_->whiteData_->name_) << " " << std::to_string(wsec) << " " << std::to_string(bsec) << " " << (newData->isMyTurn_ ? (newData->isWhite_ ? "w" : "b") : (newData->isWhite_ ? "b" : "w")) << " " << newData->chess_->getMoveHistory() << " " << "time " << newData->gameManager_->timeStamps_;
        ws->send(reconnectData.str(), uWS::OpCode::TEXT);

        if(newData->otherWS_) newData->otherWS_->send("opp-back", uWS::OpCode::TEXT);
        
        return;
    }

    PlayerData** waitingPlayer = newData->name_ == "" ? &randWaitingPlayer : &regWaitingPlayer;
    newData->ws_ = ws;

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

        ws->getUserData()->otherWS_->send(msg, uWS::OpCode::TEXT);
    }
    else if(msgType == "req-draw") {
        if(!movedPlayerData->inGame_) return;

        movedPlayerData->offeredDraw_ = true;
        ws->getUserData()->otherWS_->send(msg, uWS::OpCode::TEXT);
    }
    else if(msgType == "res-draw") {
        std::string res;
        ss >> res;

        if(res == "a") movedPlayerData->gameManager_->randGameResultHandler(true, false, "Agreement");
        else {
            movedPlayerData->otherWS_->getUserData()->offeredDraw_ = false;
            movedPlayerData->otherWS_->send("dr", uWS::OpCode::TEXT); // draw reject
        }
    }
    else if(msgType == "resign") {
        if(!movedPlayerData->inGame_) return;

        movedPlayerData->gameManager_->randGameResultHandler(false, !movedPlayerData->isWhite_, "Resignation");
    }
    // "req-" and "res-" comes to server from users
    else if(msgType == "req-rematch") {
        if(movedPlayerData->inGame_) return;

        // if other player exited or already in game
        if(!movedPlayerData->otherWS_ || movedPlayerData->otherWS_->getUserData()->inGame_) {
            ws->send("rn", uWS::OpCode::TEXT); // not available
            return;
        }


        movedPlayerData->askedRematch_ = true;
        movedPlayerData->otherWS_->send("rematch", uWS::OpCode::TEXT);
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
            movedPlayerData->otherWS_->send("rr", uWS::OpCode::TEXT); // rematch reject
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
    auto * oldData = ws->getUserData();

    if(!oldData->inGame_) {
        std::cout << "Not in game, so setting nullptr in game manager\n";
        if(oldData->isWhite_) oldData->gameManager_->whiteData_ = nullptr;
        else oldData->gameManager_->blackData_ = nullptr;

        // reset if the player was in matching phase
        if(regWaitingPlayer == oldData) regWaitingPlayer = nullptr;

        return;
    }

    // store the player data
    PlayerData* playerData = new PlayerData(std::move(*oldData));
    playerData->ws_ = nullptr;
    
    if(playerData->isWhite_) {
        playerData->gameManager_->whiteData_ = playerData;
        playerData->gameManager_->blackData_->otherWS_ = nullptr;
    }
    else {
        playerData->gameManager_->blackData_ = playerData;
        playerData->gameManager_->whiteData_->otherWS_ = nullptr;
    }

    closedConnections.insert({playerData->id_, playerData});

    playerData->startAbandonTimer();
    if(playerData->otherWS_) playerData->otherWS_->send("opp-left", uWS::OpCode::TEXT);

    std::cout << "Connection Closed: " << playerData->name_ << '\n';
} 


void handleSignupOrLogin(std::shared_ptr<bool> aborted, uWS::HttpResponse<true>* res, std::string_view body, bool isSignup) {
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

        if(!*aborted)
            res->writeStatus("404 Bad Request")->writeHeader("Content-Type", "text/plain")->end("No credentials available.");
    }

    std::string username = form["username"];
    std::string password = form["password"];

    tPool.submit([aborted, res, username, password, isSignup]() {
        std::string token;
        try {
            auto connHandle = cPool.acquire();
            pqxx::work txn(*connHandle->get());

            if (isSignup) {
                pqxx::zview query("SELECT user_name FROM player WHERE user_name = $1");
                pqxx::params params(username);
                pqxx::result r = txn.exec(query, params);

                if (!r.empty()) {
                    if(!*aborted)
                        res->writeStatus("409 Conflict")->writeHeader("Content-Type", "text/plain")->end("User already exists");
                    return;
                }


                pqxx::result tranResult = txn.exec(pqxx::zview("INSERT INTO player (user_name, password) VALUES ($1, $2) RETURNING id"), pqxx::params(username, password));
                txn.commit();
                
                int userID = tranResult[0][0].as<int>();

                token = createJWT(userID, username);
            } else {
                pqxx::zview query = "SELECT id, password FROM player WHERE user_name = $1";
                pqxx::params params(username);
                
                pqxx::result r = txn.exec(query, params);

                if (r.empty() || r[0][1].as<std::string>() != password) {
                    if(!*aborted)
                        res->writeStatus("401 Unauthorized")->writeHeader("Content-Type", "text/plain")->end("Invalid credentials.");

                    return;
                }

                int userID = r[0][0].as<int>();

                token = createJWT(userID, username);
            }

            if(!*aborted) {
                // add Secure in cookie when https is implemented
                res->writeHeader("Set-Cookie", "token=" + token + "; Path=/; HttpOnly; SameSite=Strict; Expires=Wed, 10 Jul 2025 00:00:00 GMT");
                res->writeHeader("Content-Type", "application/json");
                json j;
                j["username"] = username;
                res->end(j.dump());
            }
        } catch (const std::exception& e) {
            std::cerr << "Error in login/signup: " << e.what() << '\n';
            if(!*aborted) {
                res->writeStatus("500 Internal Server Error")->writeHeader("Content-Type", "text/plain")->end("Database error.");
            }
        }
    });
}

void gameHistoryHandler(std::shared_ptr<bool> aborted, uWS::HttpResponse<true>* res, int userID, int batchNumber, int numGames) {
    tPool.submit([=]() {
        auto connHandle = cPool.acquire();
        pqxx::work txn{*connHandle->get()};

        try {
            pqxx::result allGames = txn.exec(pqxx::zview("SELECT game_id from user_to_game WHERE user_id = $1 ORDER BY id DESC OFFSET $2 LIMIT $3"), pqxx::params(userID, batchNumber*20, numGames + 1));

            json gamesJson;
            gamesJson["games"] = json::array();

            pqxx::params gameIDs;
            for (const auto& row : allGames) {
                gameIDs.append(row[0].as<int>());
            }

            gamesJson["moreGames"] = gameIDs.size() > numGames;
            
            if(gameIDs.size()) {
                std::ostringstream query;
                query << "SELECT id, white_name, black_name, result, reason, created_at FROM game WHERE id IN (";
                for (size_t i = 0; i < gameIDs.size(); ++i) {
                    if (i != 0) query << ", ";
                    query << "$" << (i + 1);
                }
                query << ")";

                pqxx::result gamesResult = txn.exec(query.str(), gameIDs);

                json gameJson;
                for(auto game : gamesResult) {
                    gameJson["id"] = game[0].as<std::string>();
                    gameJson["white"] = game[1].as<std::string>();
                    gameJson["black"] = game[2].as<std::string>();
                    gameJson["result"] = game[3].as<std::string>();
                    gameJson["reason"] = game[4].as<std::string>();
                    gameJson["created_at"] = game[5].as<std::string>();

                    gamesJson["games"].push_back(gameJson);
                }
                if(!*aborted) {
                    res->writeHeader("Content-Type", "application/json");
                    res->end(gamesJson.dump());
                }
            }
            else {
                if(!*aborted) {
                    res->writeHeader("Content-Type", "application/json");
                    res->end("{games: {}}");
                }
            }
        } catch(std::exception& e) {
            std::cerr << "Error in game history: " << e.what() << std::endl;
            if(!*aborted) {
                res->writeStatus("500 Internal Server Error")->writeHeader("Content-Type", "text/plain")->end("Database error.");
            }
        }
    });
}


void getLiveGamesHandler(std::shared_ptr<bool> aborted, uWS::HttpResponse<true>* res, int numGames) {
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

    if(!*aborted) {
        res->writeHeader("Content-Type", "application/json");
        res->end(liveGamesJson.dump());
    }
}

void getPlayerProfile(std::shared_ptr<bool> aborted, uWS::HttpResponse<true>* res, std::string username) {
    if(username.length() == 0) {
        res->writeStatus("404 Not Found")->writeHeader("Content-Type", "text/plain")->end("Player doesn't exist. Please check the username.");
        return;
    }   

    tPool.submit([=]() {
        auto connHandle = cPool.acquire();
        pqxx::work txn{*connHandle->get()};

        try {
            pqxx::result r = txn.exec(pqxx::zview("SELECT id, joined_on FROM player WHERE user_name = $1"), pqxx::params(username));

            if(r.size() == 0) {
                if(!*aborted)
                    res->writeStatus("404 Not Found")->writeHeader("Content-Type", "text/plain")->end("Player doesn't exist. Please check the username.");
                return;
            }

            int userID = r[0][0].as<int>();

            pqxx::result allGames = txn.exec(pqxx::zview("SELECT game_id, result from user_to_game WHERE user_id = $1 ORDER BY id DESC OFFSET $2 LIMIT $3"), pqxx::params(userID, 0, 20));
            
            std::string gamesList = "";
            int numWon = 0, numLost = 0;

            json profileJson;

            profileJson["name"] = username;
            profileJson["id"] = userID;
            profileJson["joined_on"] = r[0][1].as<std::string>();

            for(auto row : allGames) {
                std::string sGameID = row[0].as<std::string>();

                gamesList += sGameID;
                gamesList.push_back(',');

                if(row[1].as<std::string>() == "w") numWon++;
                else if(row[1].as<std::string>() == "l") numLost++;
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

            if(!*aborted){
                res->writeHeader("Content-Type", "application/json");
                res->end(profileJson.dump());
            }
        } catch(std::exception& e) {
            std::cerr << "Error in player profile: " << e.what() << std::endl;
            if(!*aborted) {
                res->writeStatus("500 Internal Server Error")->writeHeader("Content-Type", "text/plain")->end("Database error.");
            }
        }
    });
}

std::string getLiveGameOfUser(int userID) {
    json liveJson;
    auto it = playersInGame.find(userID);

    if(it == playersInGame.end()) {
        liveJson["isPlaying"] = false;
        return liveJson.dump();
    }

    liveJson["isPlaying"] = true;
    liveJson["gameID"] = it->second->sGameID_;

    return liveJson.dump();
}


// only for completed games
void getGameHandler(std::shared_ptr<bool> aborted, uWS::HttpResponse<true>* res, int gameID, int reqUserID) {
    auto liveGamesIT = liveGames.find(gameID);
        
    // if it is a live game
    if(liveGamesIT != liveGames.end()) {
        if(!*aborted) res->writeStatus("404 Not Found")->writeHeader("Content-Type", "text/plain")->end("Requested game doesn't exist");
        return;
    }
    // not a live game
    else {
        tPool.submit([res, gameID, aborted]() {
            auto connHandle = cPool.acquire();
            pqxx::work txn{*connHandle->get()};

            try {
                pqxx::result gameResult = txn.exec(pqxx::zview("SELECT white_name, black_name, move_history, move_time_stamp, result, reason, created_at FROM game WHERE id = $1"), pqxx::params(gameID));

                if(gameResult.size() == 0) {
                    if(!*aborted)
                        res->writeStatus("404 Not Found")->writeHeader("Content-Type", "text/plain")->end("Requested game doesn't exist.");
                    return;
                }

                json gameJson;

                gameJson["id"] = gameID;
                gameJson["white"] = gameResult[0][0].as<std::string>();
                gameJson["black"] = gameResult[0][1].as<std::string>();
                gameJson["moves"] = gameResult[0][2].as<std::string>();
                gameJson["timeStamps"] = gameResult[0][3].as<std::string>();
                gameJson["result"] = gameResult[0][4].as<std::string>();
                gameJson["reason"] = gameResult[0][5].as<std::string>();
                gameJson["created_at"] = gameResult[0][6].as<std::string>();

                if(!*aborted)
                    res->writeHeader("Content-Type", "application/json")->end(gameJson.dump());
            }
            catch (std::exception& e) {
                std::cout << "DB Error in fetching game: " << e.what() << '\n';
                if(!*aborted)
                    res->writeHeader("Content-Type", "text/plain")->writeStatus("500 Internal Server Error")->end("Something went wrong. Please try again later.");
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
        res->writeStatus("400 Bad Request")->writeHeader("Content-Type", "text/plain")->end("Invalid topic provided.");
        return;
    }
    
    auto it = liveGames.find(stoi(topic));

    if(it == liveGames.end()) {
        res->writeStatus("404 Not Found")->writeHeader("Content-Type", "text/plain")->end("No such game to receive updates");
        return;
    }
    
    gamePointer = it->second;
    
    /* If we have this header set, it's a websocket */
    std::string_view secWebSocketKey = req->getHeader("sec-websocket-key");
    if (secWebSocketKey.length() == 24) {
        std::string_view secWebSocketProtocol = req->getHeader("sec-websocket-protocol");
        std::string_view secWebSocketExtensions = req->getHeader("sec-websocket-extensions");

        GameManagerPointer gameMngrPointer(gamePointer, gamePointer->sGameID_);

        if(!connectionAborted) 
            res->template upgrade<GameManagerPointer>(std::move(gameMngrPointer), secWebSocketKey, secWebSocketProtocol, secWebSocketExtensions, context);
        else std::cout << "Connection Aborted before upgrade\n";
    }
    else {
        std::cout << "Couldn't create web socket connection. Invalid Web Socket Key.\n";
        /* Tell the router that we did not handle this request */
        req->setYield(true);
    }
}

