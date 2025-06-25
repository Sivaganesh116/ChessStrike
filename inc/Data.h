#pragma once

#include "uv.h"
#include "App.h"
#include "libusockets.h"
#include "LegalChess.h"


#include <string>
#include <memory>

struct MoveTimer {
private:
    uv_poll_t* poll_;
    int fd_;
    int32_t timeRemainingNanoSeconds_;
    int timeRemainingSeconds_;

    MoveTimer(int timeInSeconds) {
        timeRemainingSeconds_ = timeInSeconds_;
        poll = new uv_poll_t();
        timeRemainingNanoSeconds_ = 0;

        if((fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) == -1) {
            perror("Couldn't Create Timer");
            return;
        }

        // To-Do: Poll Timer
    }

    MoveTimer(MoveTimer&& other) {
        poll_ = other.poll;
        fd_ = other.fd;
        timeRemainingNanoSeconds_ = other.timeRemainingNanoSeconds_;
        timeRemainingSeconds_ = other.timeRemainingSeconds_;

        other.poll_ = nullptr;
        other.fd_ = -1;
        other.timeRemainingNanoSeconds_ = other.timeRemainingSeconds_ = 0;
    }

    ~MoveTimer() {
        close();
    }

    int getFD() {
        return fd;
    }

    std::pair<int, uint32_t> getRemainingTime() {
        itimerspec currSpec;
        memset(&currSpec, 0, sizeof(currSpec));

        if(timerfd_gettime(fd_, &currSpec) == -1) {
            perror("Couldn't get remaining time.");
            return {0, 0};
        }

        if(currSpec.it_value.tv_sec == 0 && currSpec.it_value.tv_nsec == 0) return {timeRemainingSeconds, timeRemainingNanoSeconds};
        else return {currSpec.it_value.tv_sec, currSpec.it_value.tv_nsec};
    }

    void start() {
        itimerspec startSpec;
        memset(&startSpec, 0, sizeof(startSpec));

        startSpec.it_value.tv_nsec = timeRemainingNanoSeconds_;
        startSpec.it_value.tv_sec = timeRemainingSeconds_;

        // one-shot timer
        startSpec.it_interval.tv_nsec = 0;
        startSpec.it_interval.tv_sec = 0;

        if(timerfd_settime(fd, 0, &startSpec, NULL) == -1) {
            perror("Couldn't start the timer.");
        }
    }

    void stop() {
        auto [secs, nanoSecs] = getRemainingTime();

        itimerspec stopSpec;
        memset(&stopSpec, 0, sizeof(stopSpec));

        stopSpec.it_value.tv_nsec = 0;
        stopSpec.it_value.tv_sec = 0;
        stopSpec.it_interval.tv_nsec = 0;
        stopSpec.it_interval.tv_sec = 0;

        if(timerfd_settime(fd, 0, &stopSpec, NULL) == -1) {
            perror("Couldn't stop the timer");
            return;
        }
        
        timeRemainingSeconds_ = secs;
        timeRemainingNanoSeconds_ = nanoSecs;
    }

    uv_poll_t* getPoll() const {
        return poll_;
    }

private:
    void pollTimer(void* extData, uv_loop_t* loop, void (cb*) (uv_poll_t*, int, int)) const {
        poll_->data = extData;

        if((uv_poll_init(loop, poll_, fd)) < 0) {
            perror("uv_poll_init failed");
            return;
        }

        if(uv_poll_start(poll_, UV_READABLE, cb) < 0) {
            perror("uv_poll_start failed");
        }
    }
    
    void close() {
        if(uv_poll_stop(poll_) < 0) {
            perror("Couldn't stop timer poll");
            return;
        }

        uv_close(reinterpret_cast<uv_handle_t*>(poll_), [](uv_handle_t* h) {
            delete (uv_poll_t*)h;
        });

        if(close(fd) < 0) {
            perror("couldn't close fd");
        }
    }
};

struct PlayerData {
public:
    std::string name_;
    int id_, gameID_;
    bool isWhite_, offeredDraw_;
    uWS::WebSocket<true, true, PlayerData> *ws_, *otherWS_;
    std::unique_ptr<MoveTimer> moveTimer_;
    us_timer_t* syncTimer_;
    std::shared_ptr<LC::LegalChess> chess_;

    PlayerData() {
        name_ = "";
        id_ = -1, gameID_ = -1;
        isWhite_ = offeredDraw_ = false;
        ws_ = otherWS_ = nullptr;
        moveTimer_ = nullptr;
        syncTimer_ = nullptr;
        chess_ = nullptr;
    }

    PlayerData(PlayerData&& other) {
        name_ = other.name_;
        id_ = other.id_;
        gameID_ = other.gameID_;
        isWhite = other.isWhite;
        offeredDraw = other.offeredDraw;
        ws_ = other.ws_;
        otherWS_ = other.WS_;
        moveTimer_ = std::move(other.moveTimer_);
        syncTimer_ = other.syncTimer_;
        chess_ = other.chess_;

        other.chess_.reset();
        other.syncTimer_ = nullptr;
        other.otherWS_ = nullptr;
        other.ws_ = nullptr;
        other.isWhite_ = other.offeredDraw_ = false;
        other.id_ = other.gameId_ = -1;
        other.name_ = "";
    }
};
