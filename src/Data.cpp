#include "Data.h"

extern std::shared_ptr<uv_loop_t> uv_loop;

void timerCallback(uv_poll_t* poll, int status, int events) {
    std::cout << "timer callback\n";
    if(status < 0) {
        perror("Something went wrong with player timer expiry");
        return;
    }

    if(events & UV_READABLE) {
        auto * expiredPlayerData = static_cast<PlayerData*>(poll->data);
        if(!expiredPlayerData) {
            perror("Invalid Player Data in timer expire callback");
            return;
        }

        auto& expiredTimer = expiredPlayerData->moveTimer_;
        int fd = expiredTimer->getFD();

        // must read to close timer_fd
        uint64_t expirations;
        if(read(fd, &expirations, sizeof(expirations)) == -1) {
            perror("Couldn't read timer expirations");
            return;
        }     

        auto & chess = expiredPlayerData->chess_;
        bool isDraw = false, whiteWon = false;

        if(chess->doesColorHaveInsufficientMaterial(!expiredPlayerData->isWhite_)) {
            isDraw = true;
        }
        else {
            whiteWon = !expiredPlayerData->isWhite_;
        }

        expiredPlayerData->gameManager_->gameResultHandler(isDraw, whiteWon, "Timeout", "t");
    }
    else {
        std::cerr << "Timer event is not readable\n";
    }
}

MoveTimer::MoveTimer(int timeInSeconds, uv_loop_t* loop, void* extData) {
    initSeconds_ = timeInSeconds;
    timeRemainingSeconds_ = timeInSeconds;
    poll_ = new uv_poll_t();
    timeRemainingNanoSeconds_ = 0;

    if((fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) == -1) {
        perror("Couldn't Create Timer");
        return;
    }

    std::cout << "Move Timer fd: " << fd_ << std::endl;

    loop_ = loop;
    extData_ = extData;

    pollTimer();
}

// MoveTimer::MoveTimer(MoveTimer&& other) {
//     poll_ = other.poll_;
//     fd_ = other.fd_;
//     loop_ = other.loop_;
//     timeRemainingNanoSeconds_ = other.timeRemainingNanoSeconds_;
//     timeRemainingSeconds_ = other.timeRemainingSeconds_;
//     initSeconds_ = other.initSeconds_;

//     other.poll_ = nullptr;
//     other.fd_ = -1;
//     other.loop_ = nullptr;
//     other.timeRemainingNanoSeconds_ = other.timeRemainingSeconds_ = 0;
//     other.extData_ = nullptr;
//     other.initSeconds_ = 0;
// }

MoveTimer::~MoveTimer() {
    std::cout << "Destructor MoveTimer\n";
    closeTimer();
}

int MoveTimer::getFD() {
    return fd_;
}

std::pair<int, uint32_t> MoveTimer::getRemainingTime() {
    itimerspec currSpec;
    memset(&currSpec, 0, sizeof(currSpec));

    if(timerfd_gettime(fd_, &currSpec) == -1) {
        perror("Couldn't get remaining time.");
        return {0, 0};
    }

    if(currSpec.it_value.tv_sec == 0 && currSpec.it_value.tv_nsec == 0) return {timeRemainingSeconds_, timeRemainingNanoSeconds_};
    else return {currSpec.it_value.tv_sec, currSpec.it_value.tv_nsec};
}

void MoveTimer::start() {
    std::cout << "Move Timer start\n";

    itimerspec startSpec;
    memset(&startSpec, 0, sizeof(startSpec));

    startSpec.it_value.tv_nsec = timeRemainingNanoSeconds_;
    startSpec.it_value.tv_sec = timeRemainingSeconds_;

    // one-shot timer
    startSpec.it_interval.tv_nsec = 0;
    startSpec.it_interval.tv_sec = 0;

    if(timerfd_settime(fd_, 0, &startSpec, NULL) == -1) {
        perror("Couldn't start the timer.");
    }
}

void MoveTimer::stop() {
    auto [secs, nanoSecs] = getRemainingTime();

    itimerspec stopSpec;
    memset(&stopSpec, 0, sizeof(stopSpec));

    stopSpec.it_value.tv_nsec = 0;
    stopSpec.it_value.tv_sec = 0;
    stopSpec.it_interval.tv_nsec = 0;
    stopSpec.it_interval.tv_sec = 0;

    if(timerfd_settime(fd_, 0, &stopSpec, NULL) == -1) {
        perror("Couldn't stop the timer");
        return;
    }
    
    timeRemainingSeconds_ = secs;
    timeRemainingNanoSeconds_ = nanoSecs;
}

void MoveTimer::reset() {
    itimerspec stopSpec;
    memset(&stopSpec, 0, sizeof(stopSpec));

    stopSpec.it_value.tv_nsec = 0;
    stopSpec.it_value.tv_sec = 0;
    stopSpec.it_interval.tv_nsec = 0;
    stopSpec.it_interval.tv_sec = 0;

    if(timerfd_settime(fd_, 0, &stopSpec, NULL) == -1) {
        perror("Couldn't reset the timer");
        return;
    }

    timeRemainingSeconds_ = initSeconds_;
    timeRemainingNanoSeconds_ = 0;
}

uv_poll_t* MoveTimer::getPoll() const {
    return poll_;
}

void MoveTimer::pollTimer() const {
    std::cout << "poll fd : " << fd_ << std::endl;
    poll_->data = extData_;

    if((uv_poll_init(loop_, poll_, fd_)) < 0) {
        perror("uv_poll_init failed");
        return;
    }

    if(uv_poll_start(poll_, UV_READABLE, timerCallback) < 0) {
        perror("uv_poll_start failed");
    }
}

void MoveTimer::closeTimer() {
    if(fd_ == -1) return;

    if(uv_poll_stop(poll_) < 0) {
        perror("Couldn't stop timer poll");
        return;
    }

    uv_close(reinterpret_cast<uv_handle_t*>(poll_), [](uv_handle_t* h) {
        delete (uv_poll_t*)h;
    });

    if(close(fd_) < 0) {
        perror("couldn't close fd");
    }
}

PlayerData::PlayerData() {
    name_ = "";
    id_ = -1;
    isWhite_ = offeredDraw_ = isMyTurn_ = false;
    ws_ = otherWS_ = nullptr;
    moveTimer_ = nullptr;
    chess_ = nullptr;
    gameManager_ = nullptr;
    inGame_ = false;
    askedRematch_ = false;
    abandonTimer_ = us_create_timer((us_loop_t*) uWS::Loop::get(), 1, sizeof(PlayerData*));
    isConnected_ = false;
}

PlayerData::PlayerData(PlayerData&& other) : moveTimer_(std::move(other.moveTimer_)) {
    name_ = other.name_;
    id_ = other.id_;
    isWhite_ = other.isWhite_;
    isMyTurn_ = other.isMyTurn_;
    offeredDraw_ = other.offeredDraw_;
    ws_ = other.ws_;
    otherWS_ = other.otherWS_;
    isConnected_ = other.isConnected_;

    // set the ext data so that poll's callback won't point to player data that is about to be freed
    if(moveTimer_) moveTimer_->setExtData(this);

    abandonTimer_ = other.abandonTimer_;
    chess_ = other.chess_;
    gameManager_ = other.gameManager_;
    inGame_ = other.inGame_;
    askedRematch_ = other.askedRematch_;

    other.abandonTimer_ = nullptr;
    other.otherWS_ = nullptr;
    other.ws_ = nullptr;
    other.isWhite_ = other.offeredDraw_ = false;
    other.name_ = "";
    other.inGame_ = false;
    other.askedRematch_ = false;
    other.isMyTurn_ = false;
}

PlayerData::~PlayerData() = default;

void PlayerData::startAbandonTimer() {
    PlayerData* thisData = this;
    memcpy(us_timer_ext(abandonTimer_), &thisData, sizeof(thisData));

    us_timer_set(abandonTimer_, [](us_timer_t* timer){
        PlayerData * abandonedPlayer;

        memcpy(&abandonedPlayer, us_timer_ext(timer), sizeof(abandonedPlayer));

        if(abandonedPlayer->id_ == -1) abandonedPlayer->gameManager_->randGameResultHandler(false, !abandonedPlayer->isWhite_, "Abandonment");
        else abandonedPlayer->gameManager_->gameResultHandler(false, !abandonedPlayer->isWhite_, "Abandonment", "A");

    }, 30000, 30000);
}

void PlayerData::stopAbandonTimer() {
    us_timer_set(abandonTimer_, [](us_timer_t* timer){}, 0, 0);
}

PlayerDataPointer::PlayerDataPointer() : playerData_(nullptr) {}

PlayerDataPointer::PlayerDataPointer(PlayerData* pd) : playerData_(pd) {}

PlayerDataPointer::PlayerDataPointer(PlayerDataPointer&& other) : playerData_(other.playerData_) {
    other.playerData_ = nullptr;
}

PlayerDataPointer::~PlayerDataPointer() = default;

GameManager::GameManager(int id, PlayerData* whiteData, PlayerData* blackData) : gameID_(id), whiteData_(whiteData), blackData_(blackData), whiteScore_(0), blackScore_(0), syncTimer_(new uv_timer_t()), timeStamps_("300-300 ") {
    sGameID_ = std::to_string(id);
    syncTimer_->data = this;
    if(uv_timer_init(uv_loop.get(), syncTimer_) == -1) std::cerr << "Sync timer init failed\n";
}

GameManager::~GameManager() {
    destroySyncTimer();
}

void GameManager::gameResultHandler(bool isDraw, bool whiteWon, std::string_view reason, std::string dbReason) {
    std::string moveHistory = whiteData_->chess_->getMoveHistory();
    std::string timeStamps = timeStamps_;
    timeStamps.pop_back();

    resetData();

    std::string result;

    if(isDraw) {
        whiteScore_ += 0.5;
        blackScore_ += 0.5;
        result = "d";
    }
    else if(whiteWon) {
        whiteScore_ += 1;
        result = "w";
    }
    else {
        blackScore_ += 1;
        result = "b";
    }

    std::string toSend = "result " + result + " " + reason.data() + " " + std::to_string(whiteScore_) + " " + std::to_string(blackScore_);

    if(whiteData_->ws_) whiteData_->ws_->send(toSend, uWS::OpCode::TEXT);
    if(blackData_->ws_) blackData_->ws_->send(toSend, uWS::OpCode::TEXT);

    json publishResultJson;
    publishResultJson["type"] = "result";
    publishResultJson["result"] = (isDraw ? "d" : (whiteWon ? "w" : "b"));
    publishResultJson["reason"] = reason;
    publishResultJson["whiteScore"] = whiteScore_;
    publishResultJson["blackScore"] = blackScore_;

    app->publish(sGameID_, publishResultJson.dump(), uWS::OpCode::TEXT);

    gameResultDBHandler(dbReason, isDraw, whiteWon, moveHistory, timeStamps);    
}

void GameManager::randGameResultHandler(bool isDraw, bool whiteWon, std::string reason) {
    resetData();

    std::string result;

    if(isDraw) {
        whiteScore_ += 0.5;
        blackScore_ += 0.5;
        result = "d";
    }
    else if(whiteWon) {
        whiteScore_ += 1;
        result = "w";
    }
    else {
        blackScore_ += 1;
        result = "b";
    }

    std::string toSend = "result " + result + " " + reason.data() + " " + std::to_string(whiteScore_) + " " + std::to_string(blackScore_);

    if(whiteData_) whiteData_->ws_->send(toSend, uWS::OpCode::TEXT);
    if(blackData_) blackData_->ws_->send(toSend, uWS::OpCode::TEXT);
}


void GameManager::destroySyncTimer() {
    uv_close(reinterpret_cast<uv_handle_t*>(syncTimer_), [](uv_handle_t* handle) {
        delete reinterpret_cast<uv_timer_t*>(handle);
    });
}

void GameManager::startSyncTimer() {
    uv_timer_start(syncTimer_, [](uv_timer_t* timer){
        GameManager * gameManager = (GameManager*)timer->data;

        json timeUpdate;
        timeUpdate["type"] = "timeUpdate";
   
        auto [secw, nanow] = gameManager->whiteData_->moveTimer_->getRemainingTime();
        auto [secb, nanob] = gameManager->blackData_->moveTimer_->getRemainingTime();

        timeUpdate["black"] = std::to_string(secb) + '-' + std::to_string(nanob);
        timeUpdate["white"] = std::to_string(secw) + '-' + std::to_string(nanow);

        gameManager->whiteData_->ws_->send(timeUpdate.dump(), uWS::OpCode::TEXT);
        gameManager->blackData_->ws_->send(timeUpdate.dump(), uWS::OpCode::TEXT);
        std::cout << "Sent Time Update\n";

    }, 1000, 1000);
}

void GameManager::stopSyncTimer() {
    uv_timer_stop(syncTimer_);
}

void GameManager::gameResultDBHandler(std::string dbReason, bool isDraw, bool whiteWon, std::string moveHistory, std::string timeStamps) {
    std::cout << "DB Handler: " << dbReason << " " << moveHistory << std::endl;

    std::string gameResult, whiteResult, blackResult;

    if(isDraw) {
        gameResult = whiteResult = blackResult = "d";
    }
    else if(whiteWon) {
        gameResult = whiteResult = "w";
        blackResult = "l";
    }
    else {
        gameResult = "b";
        whiteResult = "l";
        blackResult = "w";
    }

    tPool.submit([this, dbReason, gameResult, whiteResult, blackResult, moveHistory, timeStamps]() {
        auto connHandle = cPool.acquire();
        pqxx::work txn(*connHandle->get());

        try {
            pqxx::result txnResult = txn.exec(
                pqxx::zview("UPDATE game SET move_history = $1, reason = $2, result = $3, move_time_stamp = $4 WHERE id = $5;"),
                pqxx::params(moveHistory, dbReason, gameResult, timeStamps, gameID_)
            );

            txn.exec(
                pqxx::zview("INSERT INTO user_to_game (user_id, game_id, result) "
                "VALUES ($1, $2, $3)"),
                pqxx::params(whiteData_->id_, gameID_, whiteResult)
            );

            txn.exec(
                pqxx::zview("INSERT INTO user_to_game (user_id, game_id, result) "
                "VALUES ($1, $2, $3)"),
                pqxx::params(blackData_->id_, gameID_, blackResult)
            );

            txn.commit();
        } catch (const std::exception& e) {
            std::cerr << "Error handling db operations of game result of game id: " << gameID_ << ". " << e.what() << std::endl;
        }
    });

    // clean up memory if players had left the game
    if(!whiteData_->ws_) {
        closedConnections.erase(whiteData_->id_);
        delete whiteData_;
    }

    if(!blackData_->ws_) {
        closedConnections.erase(blackData_->id_);
        delete blackData_;
    }
}

void GameManager::resetData() {
    stopSyncTimer();
    whiteData_->moveTimer_->reset();
    blackData_->moveTimer_->reset();
    whiteData_->stopAbandonTimer();
    blackData_->stopAbandonTimer();

    playersInGame.erase(whiteData_->id_);
    playersInGame.erase(blackData_->id_);
    liveGames.erase(gameID_);

    blackData_->chess_ = whiteData_->chess_ = std::make_shared<LC::LegalChess>();
    whiteData_->askedRematch_ = whiteData_->inGame_ = whiteData_->isWhite_ = whiteData_->offeredDraw_ = false;
    blackData_->askedRematch_ = blackData_->inGame_ = blackData_->isWhite_ = blackData_->offeredDraw_ = false;
    timeStamps_ = "300-300 ";
}

GameManagerPointer::GameManagerPointer() : pointer(nullptr), topic("") {}

GameManagerPointer::GameManagerPointer(GameManager* p, std::string sTopic) : pointer(p), topic(sTopic) {}

GameManagerPointer::GameManagerPointer(GameManagerPointer&& other) {
    std::cout << "move Constructor gp\n";
    
    pointer = other.pointer;
    topic = other.topic;

    other.pointer = nullptr;
    other.topic = "";
}
