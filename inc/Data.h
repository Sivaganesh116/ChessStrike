// #pragma once

// #include "uv.h"
// #include "App.h"
// #include "libusockets.h"
// #include "LegalChess.h"
// #include "jwt.h"
// #include <pqxx/pqxx>
// #include <nlohmann/json.hpp>

// #include <string>
// #include <memory>
// #include <sys/timerfd.h>
// #include <sys/epoll.h>

// #include "ThreadPool.h"
// #include "ConnectionPool.h"

// #include <unordered_map>

// extern ThreadPool tPool;
// extern PostgresConnectionPool cPool;
// extern std::unordered_map<int, GameManager*> liveGames;
// extern std::unordered_map<int, GameManager*> playersInGame;

// using json = nlohmann::json;

// struct PlayerData;

// void timerCallback(uv_poll_t* poll, int status, int events) {
//     if(status < 0) {
//         perror("Something went wrong with player timer expiry");
//         return;
//     }

//     if(events & UV_READABLE) {
//         auto * expiredPlayerData = static_cast<PlayerData*>(poll->data);
//         if(!expiredPlayerData) {
//             perror("Invalid Player Data in timer expire callback");
//             return;
//         }

//         auto& expiredTimer = expiredPlayerData->moveTimer_;

//         int fd = expiredTimer->getFD();

//         // must read to close timer_fd
//         uint64_t expirations;
//         if(read(fd, &expirations, sizeof(expirations)) == -1) {
//             perror("Couldn't read timer expirations");
//             return;
//         }     

//         auto & chess = expiredPlayerData->chess_;
//         bool isDraw = false, whiteWon = false;

//         if(chess->doesColorHaveInsufficientMaterial(!expiredPlayerData->isWhite_)) {
//             isDraw = true;
//         }
//         else {
//             whiteWon = !expiredPlayerData->isWhite_;
//         }

//         expiredPlayerData->gameManager_->gameResultHandler(isDraw, whiteWon, "Timeout", 't');
//     }
// }

// struct MoveTimer {
//     uv_poll_t* poll_;
//     int fd_;
//     int32_t timeRemainingNanoSeconds_;
//     int timeRemainingSeconds_;
//     uv_loop_t* loop_;
//     void* extData_;
//     int initSeconds_;


//     MoveTimer(int timeInSeconds, uv_loop_t* loop, void* extData) {
//         initSeconds_ = timeRemainingSeconds_;
//         timeRemainingSeconds_ = timeInSeconds;
//         poll_ = new uv_poll_t();
//         timeRemainingNanoSeconds_ = 0;

//         if((fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) == -1) {
//             perror("Couldn't Create Timer");
//             return;
//         }

//         loop_ = loop;
//         extData_ = extData;

//         // Poll Timer
//         pollTimer();
//     }

//     MoveTimer(MoveTimer&& other) {
//         poll_ = other.poll_;
//         fd_ = other.fd_;
//         timeRemainingNanoSeconds_ = other.timeRemainingNanoSeconds_;
//         timeRemainingSeconds_ = other.timeRemainingSeconds_;

//         other.poll_ = nullptr;
//         other.fd_ = -1;
//         other.timeRemainingNanoSeconds_ = other.timeRemainingSeconds_ = 0;
//     }

//     ~MoveTimer() {
//         closeTimer();
//     }

//     int getFD() {
//         return fd_;
//     }

//     std::pair<int, uint32_t> getRemainingTime() {
//         itimerspec currSpec;
//         memset(&currSpec, 0, sizeof(currSpec));

//         if(timerfd_gettime(fd_, &currSpec) == -1) {
//             perror("Couldn't get remaining time.");
//             return {0, 0};
//         }

//         if(currSpec.it_value.tv_sec == 0 && currSpec.it_value.tv_nsec == 0) return {timeRemainingSeconds_, timeRemainingNanoSeconds_};
//         else return {currSpec.it_value.tv_sec, currSpec.it_value.tv_nsec};
//     }

//     void start() {
//         itimerspec startSpec;
//         memset(&startSpec, 0, sizeof(startSpec));

//         startSpec.it_value.tv_nsec = timeRemainingNanoSeconds_;
//         startSpec.it_value.tv_sec = timeRemainingSeconds_;

//         // one-shot timer
//         startSpec.it_interval.tv_nsec = 0;
//         startSpec.it_interval.tv_sec = 0;

//         if(timerfd_settime(fd_, 0, &startSpec, NULL) == -1) {
//             perror("Couldn't start the timer.");
//         }
//     }

//     void stop() {
//         auto [secs, nanoSecs] = getRemainingTime();

//         itimerspec stopSpec;
//         memset(&stopSpec, 0, sizeof(stopSpec));

//         stopSpec.it_value.tv_nsec = 0;
//         stopSpec.it_value.tv_sec = 0;
//         stopSpec.it_interval.tv_nsec = 0;
//         stopSpec.it_interval.tv_sec = 0;

//         if(timerfd_settime(fd_, 0, &stopSpec, NULL) == -1) {
//             perror("Couldn't stop the timer");
//             return;
//         }
        
//         timeRemainingSeconds_ = secs;
//         timeRemainingNanoSeconds_ = nanoSecs;
//     }

//     void reset() {
//         itimerspec stopSpec;
//         memset(&stopSpec, 0, sizeof(stopSpec));

//         stopSpec.it_value.tv_nsec = 0;
//         stopSpec.it_value.tv_sec = 0;
//         stopSpec.it_interval.tv_nsec = 0;
//         stopSpec.it_interval.tv_sec = 0;

//         if(timerfd_settime(fd_, 0, &stopSpec, NULL) == -1) {
//             perror("Couldn't reset the timer");
//             return;
//         }

//         timeRemainingSeconds_ = initSeconds_;
//         timeRemainingNanoSeconds_ = 0;
//     }

//     uv_poll_t* getPoll() const {
//         return poll_;
//     }

// private:
//     void pollTimer() const {
//         poll_->data = extData_;

//         if((uv_poll_init(loop_, poll_, fd_)) < 0) {
//             perror("uv_poll_init failed");
//             return;
//         }

//         if(uv_poll_start(poll_, UV_READABLE, timerCallback) < 0) {
//             perror("uv_poll_start failed");
//         }
//     }
    
//     void closeTimer() {
//         if(uv_poll_stop(poll_) < 0) {
//             perror("Couldn't stop timer poll");
//             return;
//         }

//         uv_close(reinterpret_cast<uv_handle_t*>(poll_), [](uv_handle_t* h) {
//             delete (uv_poll_t*)h;
//         });

//         if(close(fd_) < 0) {
//             perror("couldn't close fd");
//         }
//     }
// };

// struct GameManager;

// struct PlayerData {
// public:
//     std::string name_;
//     int id_;
//     bool isWhite_, offeredDraw_;
//     uWS::WebSocket<true, true, PlayerData> *ws_, *otherWS_;
//     std::unique_ptr<MoveTimer> moveTimer_;
//     std::shared_ptr<LC::LegalChess> chess_;
//     std::shared_ptr<GameManager> gameManager_;
//     bool inGame_, askedRematch_;
//     us_timer_t* abandonTimer_;

//     PlayerData() {
//         name_ = "";
//         id_ = -1;
//         isWhite_ = offeredDraw_ = false;
//         ws_ = otherWS_ = nullptr;
//         moveTimer_ = nullptr;
//         chess_ = nullptr;
//         gameManager_ = nullptr;
//         inGame_ = false;
//         askedRematch_ = false;
//         abandonTimer_ = us_create_timer((us_loop_t*) uWS::Loop::get(), 1, sizeof(PlayerData*));

//         PlayerData* thisData = this;
//         memcpy(us_timer_ext(abandonTimer_), &thisData, sizeof(thisData));
//     }

//     PlayerData(PlayerData&& other) {
//         name_ = other.name_;
//         id_ = other.id_;
//         isWhite_ = other.isWhite_;
//         offeredDraw_ = other.offeredDraw_;
//         ws_ = other.ws_;
//         otherWS_ = other.otherWS_;
//         moveTimer_ = std::move(other.moveTimer_);
//         abandonTimer_ = other.abandonTimer_;
//         chess_ = other.chess_;
//         gameManager_ = other.gameManager_;
//         inGame_ = other.inGame_;
//         askedRematch_ = other.askedRematch_;

//         other.gameManager_.reset();
//         other.chess_.reset();
//         other.abandonTimer_ = nullptr;
//         other.otherWS_ = nullptr;
//         other.ws_ = nullptr;
//         other.isWhite_ = other.offeredDraw_ = false;
//         other.name_ = "";
//         other.inGame_ = false;
//         other.askedRematch_ = false;
//     }

//     void startAbandonTimer() {
//         us_timer_set(abandonTimer_, [](us_timer_t* timer){
//             PlayerData * abandonedPlayer;

//             memcpy(&abandonedPlayer, us_timer_ext(timer), sizeof(abandonedPlayer));

//             // if rand game
//             if(abandonedPlayer->id_ == -1) abandonedPlayer->gameManager_->randGameResultHandler(false, !abandonedPlayer->isWhite_, "Abandonment");
//             else abandonedPlayer->gameManager_->gameResultHandler(false, !abandonedPlayer->isWhite_, "Abandonment", 'a');

//         }, 30000, 30000);
//     }

//     void stopAbandonTimer() {
//         us_timer_set(abandonTimer_, [](us_timer_t* timer){}, 0, 0);
//     }
// };


// struct GameManager {
// public:
//     GameManager(int id, PlayerData* whiteData, PlayerData* blackData) : gameID_(id), whiteData_(whiteData), blackData_(blackData), whiteScore_(0), blackScore_(0) {
//         sGameID_ = std::to_string(id);
//         createSyncTimer();
//     }

//     void gameResultHandler(bool isDraw, bool whiteWon, std::string_view reason, char dbReason) {
//         json resultJson;
//         resultJson["type"] = "result";
//         resultJson["reason"] = reason;

//         if(isDraw) {
//             whiteScore_ += 0.5;
//             blackScore_ += 0.5;

//             resultJson["w_score"] = whiteScore_;
//             resultJson["b_score"] = blackScore_;

//             resultJson["result"] = "draw";
//             whiteData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
//             blackData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);

//             whiteData_->ws_->publish(whiteData_->gameManager_->sGameID_, resultJson.dump(), uWS::OpCode::TEXT);

//             return;
//         }
        
//         resultJson["result"] = "win";

//         if(whiteWon) {
//             whiteScore_ += 1;

//             resultJson["w_score"] = whiteScore_;
//             resultJson["b_score"] = blackScore_;

//             whiteData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
//             resultJson["result"] = "lose";
//             blackData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);

//             resultJson["result"] = "white";

//             whiteData_->ws_->publish(whiteData_->gameManager_->sGameID_, resultJson.dump(), uWS::OpCode::TEXT);
//         }
//         else {
//             blackScore_ += 1;

//             resultJson["w_score"] = whiteScore_;
//             resultJson["b_score"] = blackScore_;

//             blackData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
//             resultJson["result"] = "lose";
//             whiteData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);

//             resultJson["result"] = "white";

//             whiteData_->ws_->publish(whiteData_->gameManager_->sGameID_, resultJson.dump(), uWS::OpCode::TEXT);
//         }

//         std::string moveHistory = whiteData_->chess_->getMoveHistory();

//         resetData();

//         gameResultDBHandler(dbReason, isDraw, whiteWon, moveHistory);    
//     }

//     void randGameResultHandler(bool isDraw, bool whiteWon, std::string reason) {
//         resetData();

//         json resultJson;
//         resultJson["type"] = "result";
//         resultJson["reason"] = reason;

//         if(isDraw) {
//             resultJson["result"] = "draw";
//             whiteData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
//             blackData_->otherWS_->send(resultJson.dump(), uWS::OpCode::TEXT);
//             return;
//         }

//         if(whiteWon) {
//             resultJson["result"] = "win";
//             whiteData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
//             resultJson["result"] = "lose";
//             blackData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
//         }
//         else {
//             resultJson["result"] = "win";
//             blackData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
//             resultJson["result"] = "lose";
//             whiteData_->ws_->send(resultJson.dump(), uWS::OpCode::TEXT);
//         }
//     }

//     int gameID_;
//     std::string sGameID_;
//     PlayerData* whiteData_;
//     PlayerData* blackData_;
//     float whiteScore_, blackScore_;
//     us_timer_t* syncTimer_;

// private:
//     void createSyncTimer() {
//         syncTimer_ = us_create_timer((us_loop_t*)uWS::Loop::get(), 1, sizeof(GameManager*));
//         GameManager* thisData = this;
//         memcpy(us_timer_ext(syncTimer_), &thisData, sizeof(thisData));
        

//         us_timer_set(syncTimer_, [](us_timer_t* timer){
//             GameManager * gameManager;

//             memcpy(&gameManager, us_timer_ext(timer), sizeof(gameManager));

//             json timeUpdate;
//             timeUpdate["type"] = "timeUpdate";
       
//             auto [secw, nanow] = gameManager->whiteData_->moveTimer_->getRemainingTime();
//             auto [secb, nanob] = gameManager->blackData_->moveTimer_->getRemainingTime();

//             timeUpdate["black"] = std::to_string(secb) + '-' + std::to_string(nanob);
//             timeUpdate["white"] = std::to_string(secw) + '-' + std::to_string(nanow);

//             gameManager->whiteData_->ws_->send(timeUpdate.dump(), uWS::OpCode::TEXT);
//             gameManager->blackData_->ws_->send(timeUpdate.dump(), uWS::OpCode::TEXT);

//         }, 1000, 1000);
//     }

//     void destroySyncTimer() {
//         us_timer_close(syncTimer_);
//         syncTimer_ = nullptr;
//     }

//     void gameResultDBHandler(char dbReason, bool isDraw, bool whiteWon, std::string moveHistory) {
//         char gameResult, whiteResult, blackResult;

//         if(isDraw) {
//             gameResult = whiteResult = blackResult = 'd';
//         }
//         else if(whiteWon) {
//             gameResult = whiteResult = 'w';
//             blackResult = 'l';
//         }
//         else {
//             gameResult = 'b';
//             whiteResult = 'l';
//             blackResult = 'w';
//         }

//         tPool.submit([this, dbReason, gameResult, whiteResult, blackResult, moveHistory]() {
//             auto connHandle = cPool.acquire();
//             pqxx::work txn(*connHandle->get());

//             try {
//                 // update table "game"
//                 pqxx::result txnResult = txn.exec(
//                     pqxx::zview("UPDATE game SET move_history = $1, reason = $2, result = $3 WHERE game_id = $4;"),
//                     pqxx::params(moveHistory, dbReason, gameResult, gameID_)
//                 );

//                 // insert entry into "user_to_game"
//                 txn.exec(
//                     pqxx::zview("INSERT INTO user_to_game (user_id, game_id, result) "
//                     "VALUES ($1, $2, $3)"),
//                     pqxx::params(whiteData_->id_, gameID_, whiteResult)
//                 );

//                 txn.exec(
//                     pqxx::zview("INSERT INTO user_to_game (user_id, game_id, result) "
//                     "VALUES ($1, $2, $3)"),
//                     pqxx::params(blackData_->id_, gameID_, blackResult)
//                 );

//                 // To-Do: check for "error thrown: "in_doubt_error"
//                 txn.commit();
//             } catch (const std::exception& e) {
//                 std::cerr << "Error handling db operations of game result of game id: " << gameID_ << ". " << e.what() << std::endl;
//             }
//         });
//     }

//     void resetData() {
//         destroySyncTimer();
//         whiteData_->moveTimer_->reset();
//         blackData_->moveTimer_->reset();
//         whiteData_->stopAbandonTimer();
//         blackData_->stopAbandonTimer();

//         playersInGame.erase(whiteData_->id_);
//         playersInGame.erase(blackData_->id_);
//         liveGames.erase(gameID_);

//         blackData_->chess_ = whiteData_->chess_ = std::make_shared<LC::LegalChess>();
//         whiteData_->askedRematch_ = whiteData_->inGame_ = whiteData_->isWhite_ = whiteData_->offeredDraw_ = false;
//         blackData_->askedRematch_ = blackData_->inGame_ = blackData_->isWhite_ = blackData_->offeredDraw_ = false;
//     }
// };

// struct GameManagerPointer {
//     GameManager* pointer;

//     GameManagerPointer() {
//         pointer = nullptr;
//     }
// };


#pragma once

#include "uv.h"
#include "App.h"
#include "libusockets.h"
#include "LegalChess.h"
#include "jwt.h"
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>

#include <string>
#include <memory>
#include <sys/timerfd.h>
#include <sys/epoll.h>

#include "ThreadPool.h"
#include "ConnectionPool.h"

#include <unordered_map>

struct PlayerData;
struct GameManager;

extern ThreadPool tPool;
extern PostgresConnectionPool cPool;
extern std::unordered_map<int, GameManager*> liveGames;
extern std::unordered_map<int, GameManager*> playersInGame;

using json = nlohmann::json;

void timerCallback(uv_poll_t* poll, int status, int events);

struct MoveTimer {
    uv_poll_t* poll_;
    int fd_;
    int32_t timeRemainingNanoSeconds_;
    int timeRemainingSeconds_;
    uv_loop_t* loop_;
    void* extData_;
    int initSeconds_;

    MoveTimer(int timeInSeconds, uv_loop_t* loop, void* extData);
    MoveTimer(MoveTimer&& other);
    ~MoveTimer();

    int getFD();
    std::pair<int, uint32_t> getRemainingTime();
    void start();
    void stop();
    void reset();
    uv_poll_t* getPoll() const;

private:
    void pollTimer() const;
    void closeTimer();
};

struct PlayerData {
public:
    std::string name_;
    int id_;
    bool isWhite_, offeredDraw_;
    uWS::WebSocket<true, true, PlayerData> *ws_, *otherWS_;
    std::unique_ptr<MoveTimer> moveTimer_;
    std::shared_ptr<LC::LegalChess> chess_;
    std::shared_ptr<GameManager> gameManager_;
    bool inGame_, askedRematch_;
    us_timer_t* abandonTimer_;

    PlayerData();
    PlayerData(PlayerData&& other);

    void startAbandonTimer();
    void stopAbandonTimer();
};

struct GameManager {
public:
    GameManager(int id, PlayerData* whiteData, PlayerData* blackData);

    void gameResultHandler(bool isDraw, bool whiteWon, std::string_view reason, std::string dbReason);
    void randGameResultHandler(bool isDraw, bool whiteWon, std::string reason);

    int gameID_;
    std::string sGameID_;
    PlayerData* whiteData_;
    PlayerData* blackData_;
    float whiteScore_, blackScore_;
    us_timer_t* syncTimer_;

private:
    void createSyncTimer();
    void destroySyncTimer();
    void gameResultDBHandler(std::string dbReason, bool isDraw, bool whiteWon, std::string moveHistory);
    void resetData();
};

struct GameManagerPointer {
    GameManager* pointer;

    GameManagerPointer();

    GameManagerPointer(GameManager* p);

    GameManagerPointer(GameManagerPointer&& other);
};
