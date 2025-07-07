#pragma once

#include "uv.h"
#include "App.h"
#include "uv.h"
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
extern std::unordered_map<int, PlayerData*> closedConnections;

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
    // MoveTimer(MoveTimer&& other);
    ~MoveTimer();

    int getFD();
    std::pair<int, uint32_t> getRemainingTime();
    void start();
    void stop();
    void reset();
    uv_poll_t* getPoll() const;
    
    inline void setExtData(void* extData) {
        poll_->data = extData;
    }

private:
    void pollTimer() const;
    void closeTimer();
};

struct PlayerData {
public:
    std::string name_;
    int id_;
    bool isWhite_, offeredDraw_, isMyTurn_;
    uWS::WebSocket<true, true, PlayerData> *ws_, *otherWS_;
    std::unique_ptr<MoveTimer> moveTimer_;
    std::shared_ptr<LC::LegalChess> chess_;
    std::shared_ptr<GameManager> gameManager_;
    bool inGame_, askedRematch_;
    us_timer_t* abandonTimer_;

    PlayerData();
    ~PlayerData();
    PlayerData(PlayerData&& other);

    void startAbandonTimer();
    void stopAbandonTimer();
};

struct GameManager {
public:
    GameManager(int id, PlayerData* whiteData, PlayerData* blackData);
    ~GameManager();

    void gameResultHandler(bool isDraw, bool whiteWon, std::string_view reason, std::string dbReason);
    void randGameResultHandler(bool isDraw, bool whiteWon, std::string reason);
    void startSyncTimer();
    void stopSyncTimer();
    void resetData();

    int gameID_;
    std::string sGameID_;
    PlayerData* whiteData_;
    PlayerData* blackData_;
    float whiteScore_, blackScore_;
    std::string timeStamps_;
    uv_timer_t* syncTimer_;

private:
    void destroySyncTimer();
    void gameResultDBHandler(std::string dbReason, bool isDraw, bool whiteWon, std::string moveHistory, std::string timeStamps);
};

struct GameManagerPointer {
    GameManager* pointer;
    std::string topic;

    GameManagerPointer();

    GameManagerPointer(GameManager* p, std::string topic);

    GameManagerPointer(GameManagerPointer&& other);
};
