#include <iostream>
#include <pqxx/pqxx>
#include <App.h>
#include <nlohmann/json.hpp>
#include <uv.h>

#include "jwt.h"

#include "LegalChess.h"
#include "concurrentqueue.h"
#include "ThreadPool.h"
#include "ConnectionPool.h"

#include <string>
#include <sstream>
#include <fstream>

#include <sys/timerfd.h>
#include <sys/epoll.h>

using json = nlohmann::json;
struct PlayerData;

PlayerData* waitingPlayer = nullptr, *regWaitingPlayer = nullptr;
std::shared_ptr<uv_loop_t> uv_loop = nullptr;

const std::string connStr = "host=localhost port=5432 dbname=chess user=postgres password=yourpassword";
PostgresConnectionPool pool(connStr, 10);

ThreadPool threadPool(2);

// JWT secret key
const std::string jwtSecret = "your_secret_key";


struct TimerData {
private:
    int fd = -1;
    int timeRemainingSeconds = 0, timeRemainingNanoSeconds = 0; // used to store remaining time when player's turn is over
    uv_poll_t* poll = nullptr;

public:
    TimerData(int timeInMinutes, PlayerData* playerData) {
        if((fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) == -1) {
            throw std::runtime_error("Couldn't Create Timer");
        }

        timeRemainingSeconds = timeInMinutes * 60; 
    }

    ~TimerData() {
        closeTimer();
    }

    bool stopTimer() {
        auto [secs, nanoSecs] = getRemainingTime();

        itimerspec stopSpec;
        memset(&stopSpec, 0, sizeof(stopSpec));

        stopSpec.it_value.tv_nsec = 0;
        stopSpec.it_value.tv_sec = 0;
        stopSpec.it_interval.tv_nsec = 0;
        stopSpec.it_interval.tv_sec = 0;

        if(timerfd_settime(fd, 0, &stopSpec, NULL) == -1) {
            perror("Couldn't stop the timer");
            return false;
        }
        
        timeRemainingSeconds = secs;
        timeRemainingNanoSeconds = nanoSecs;

        return true;
    }

    std::pair<int,int> getRemainingTime() const {
        itimerspec currSpec;
        memset(&currSpec, 0, sizeof(currSpec));

        if(timerfd_gettime(fd, &currSpec) == -1) {
            perror("Couldn't get remaining time.");
            return {0, 0};
        }

        if(currSpec.it_value.tv_sec == 0 && currSpec.it_value.tv_nsec == 0) return {timeRemainingSeconds, timeRemainingNanoSeconds};
        else return {currSpec.it_value.tv_sec, currSpec.it_value.tv_nsec};
    }

    bool startTimer() {
        itimerspec startSpec;
        memset(&startSpec, 0, sizeof(startSpec));

        startSpec.it_value.tv_nsec = timeRemainingNanoSeconds;
        startSpec.it_value.tv_sec = timeRemainingSeconds;

        // one-shot timer
        startSpec.it_interval.tv_nsec = 0;
        startSpec.it_interval.tv_sec = 0;

        if(timerfd_settime(fd, 0, &startSpec, NULL) == -1) {
            perror("Couldn't stop the timer");
            return false;
        }

        return true;
    }

    void closeTimer() {
        if(fd != -1) {
            if(close(fd) < 0) {
                perror("couldn't close fd");
            }
            fd = -1;
        }
    }

    int getFD() const {
        return fd;
    }

    void setPoll(uv_poll_t* poll) {
        this->poll = poll;
    }

    uv_poll_t* getPoll() const {
        return poll;
    }
};

struct PlayerData {
    std::string name;
    int id;
    std::shared_ptr<LC::LegalChess> chess;
    uWS::WebSocket<true, true, PlayerData>* ws, *otherWs;
    bool isWhite;
    bool isMyTurn;
    std::unique_ptr<TimerData> timerData;
    us_timer_t* syncTimer;
    bool authDone;


    PlayerData() {
        name = "";
        id = -1;
        chess = nullptr;
        ws = nullptr;
        otherWs = nullptr;
        isWhite = false;
        isMyTurn = false;
        timerData = nullptr;
        syncTimer = nullptr;
        authDone = false;
    }

    PlayerData(PlayerData&& other) {
        this->name = other.name;
        this->id = other.id;
        this->chess = other.chess;
        this->ws = other.ws;
        this->otherWs = other.otherWs;
        this->isWhite = other.isWhite;
        this->timerData = std::move(other.timerData);
        this->syncTimer = other.syncTimer;
    }

    ~PlayerData() = default;

    void createSyncTimer() {
        syncTimer = us_create_timer((us_loop_t*)uWS::Loop::get(), 1, sizeof(PlayerData*));
        PlayerData* thisData = this;
        memcpy(us_timer_ext(syncTimer), &thisData, sizeof(thisData));
        

        us_timer_set(syncTimer, [](us_timer_t* timer){
            PlayerData * playerData1;

            memcpy(&playerData1, us_timer_ext(timer), sizeof(playerData1));

            json timeUpdate;
            timeUpdate["type"] = "timeUpdate";

            auto * playerData2 = playerData1->otherWs->getUserData();

            

            auto [sec1, nano1] = playerData1->timerData->getRemainingTime();
            auto [sec2, nano2] = playerData2->timerData->getRemainingTime();

            if(playerData1->isWhite) {
                timeUpdate["white"] = std::to_string(sec1) + '-' + std::to_string(nano1);
                timeUpdate["black"] = std::to_string(sec2) + '-' + std::to_string(nano2);
            }
            else {
                timeUpdate["black"] = std::to_string(sec1) + '-' + std::to_string(nano1);
                timeUpdate["white"] = std::to_string(sec2) + '-' + std::to_string(nano2);
            }

            playerData1->ws->send(timeUpdate.dump(), uWS::OpCode::TEXT);
            playerData2->ws->send(timeUpdate.dump(), uWS::OpCode::TEXT);

            

        }, 2000, 2000);
    }

    void createSyncTimer(us_timer_t* timer) {
        syncTimer = timer;
    }
};


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

void playerTimerCallBack(uv_poll_t* handle, int status, int events) {
    if(status < 0) {
        perror("Something went wrong with player timer expiry");
        return;
    }

    if(events & UV_READABLE) {
        auto * expiredPlayerData = static_cast<PlayerData*>(handle->data);

        if(!expiredPlayerData) {
            perror("Invalid Player Data in timer expire callback");
            return;
        }

        auto& expiredTimerData = expiredPlayerData->timerData;

        int fd = expiredTimerData->getFD();

        // must read to close timer_fd
        uint64_t expirations;
        if(read(fd, &expirations, sizeof(expirations)) == -1) {
            perror("Couldn't read timer expirations");
            return;
        }

        // close client sync timer
        us_timer_close(expiredPlayerData->syncTimer);

        // close the expired timer
        if(uv_poll_stop(handle) < 0) {
            perror("Couldn't stop timer poll");
        }

        uv_close(reinterpret_cast<uv_handle_t*>(handle), [](uv_handle_t* h) {
            delete (uv_poll_t*)h;
        });


        // close the other player's running timer
        auto& runningTimerData = expiredPlayerData->otherWs->getUserData()->timerData;

        if(uv_poll_stop(runningTimerData->getPoll()) < 0) {
            perror("Couldn't stop timer poll");
        }

        uv_close(reinterpret_cast<uv_handle_t*>(runningTimerData->getPoll()), [](uv_handle_t* h) {
            delete (uv_poll_t*)h;
        });

        auto & chess = expiredPlayerData->chess;
        bool amIWhite = expiredPlayerData->isWhite;

        json resultJson;
        resultJson["type"] = "result";

        if(chess->doesColorHaveInsufficientMaterial(!amIWhite)) {
            resultJson["result"] = "draw";
            resultJson["reason"] = "Timeout";

            expiredPlayerData->ws->send(resultJson.dump(), uWS::OpCode::TEXT);
            expiredPlayerData->otherWs->send(resultJson.dump(), uWS::OpCode::TEXT);
        }
        else {
            resultJson["result"] = "win";
            resultJson["reason"] = "Timeout";
            expiredPlayerData->otherWs->send(resultJson.dump(), uWS::OpCode::TEXT);

            resultJson["result"] = "lose";
            resultJson["reason"] = "Timeout";
            expiredPlayerData->ws->send(resultJson.dump(), uWS::OpCode::TEXT);
        }
    }
}

// Poll custom fd in the libuv event loop, make sure libuv's loop will be used by uWS as well
void pollFd(int fd, void* extData, uv_loop_t* loop, void (*cb) (uv_poll_t*, int, int)) {
    uv_poll_t* handle = new uv_poll_t();
    handle->data = extData;

    PlayerData* playerData = (PlayerData*)extData;
    playerData->timerData->setPoll(handle);

    if((uv_poll_init(loop, handle, fd)) < 0) {
        perror("uv_poll_init failed");

    }
    if(uv_poll_start(handle, UV_READABLE, cb) < 0) {
        perror("uv_poll_start failed");
    }
}


void startGame(PlayerData* newPlayer) {
    // Create a random device and a random number generator
    std::random_device rd;  // Seed
    std::mt19937 gen(rd()); // Mersenne Twister engine

    // Define the distribution range [1, 20]
    std::uniform_int_distribution<> dist(1, 20);

    // Generate random number
    int random_number = dist(gen);

    if(random_number % 2) {
        if(waitingPlayer->name == "") waitingPlayer->name = "white";
        waitingPlayer->id = 1;
        waitingPlayer->isWhite = true;
        waitingPlayer->isMyTurn = true;

        if(newPlayer->name == "") newPlayer->name = "black";
        newPlayer->id = 0;
        newPlayer->isWhite = false;
        newPlayer->isMyTurn = false;
    }
    else {
        if(waitingPlayer->name == "") waitingPlayer->name = "black";
        waitingPlayer->id = 0;
        waitingPlayer->isWhite = false;
        waitingPlayer->isMyTurn = false;

        if(newPlayer->name == "") newPlayer->name = "white";
        newPlayer->id = 1;
        newPlayer->isWhite = true;
        newPlayer->isMyTurn = true;
    }

    waitingPlayer->chess = newPlayer->chess = std::make_shared<LC::LegalChess>();
    waitingPlayer->otherWs = newPlayer->ws;
    newPlayer->otherWs = waitingPlayer->ws;

    waitingPlayer->timerData = std::make_unique<TimerData>(1, waitingPlayer);
    newPlayer->timerData = std::make_unique<TimerData>(1, newPlayer);


    json toNewPlayer;
    toNewPlayer["type"] = "start-game";
    toNewPlayer["white"] = newPlayer->isWhite;

    json toWaitingPlayer;
    toWaitingPlayer["type"] = "start-game";
    toWaitingPlayer["white"] = waitingPlayer->isWhite;

    std::cout << "player's matched" << std::endl;

    newPlayer->ws->send(toNewPlayer.dump(), uWS::OpCode::TEXT);
    waitingPlayer->ws->send(toWaitingPlayer.dump(), uWS::OpCode::TEXT);

    // To-Do: should i call this after starting player timers?
    newPlayer->createSyncTimer();
    waitingPlayer->createSyncTimer(newPlayer->syncTimer);

    if(random_number % 2) waitingPlayer->timerData->startTimer();
    else newPlayer->timerData->startTimer();

    // poll the individual player timers
    pollFd(waitingPlayer->timerData->getFD(), (void *)(waitingPlayer), (uv_loop_t*)uv_loop.get(), playerTimerCallBack);
    pollFd(newPlayer->timerData->getFD(), (void *)(newPlayer), (uv_loop_t*)uv_loop.get(), playerTimerCallBack);

    // reset the waiting player
    waitingPlayer = nullptr;
}


std::string createJWT(const std::string& username) {
    auto token = jwt::create()
        .set_type("JWT")
        .set_issuer("chess-strike")
        .set_payload_claim("username", jwt::claim(username))
        .sign(jwt::algorithm::hs256{jwtSecret});
    return token;
}

void handleSignupOrLogin(uWS::HttpResponse<true>* res, std::string_view body, PostgresConnectionPool& pool, ThreadPool& threadPool, bool isSignup) {
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

    std::string username = form["username"];
    std::string password = form["password"];

    threadPool.submit([&pool, res, username, password, isSignup]() {
        try {
            auto connHandle = pool.acquire();
            pqxx::work txn(*connHandle->get());

            if (isSignup) {
                pqxx::zview query("SELECT username FROM player WHERE username = $1");
                pqxx::params params(username);
                pqxx::result r = txn.exec(query, params);

                if (!r.empty()) {
                    res->writeStatus("409 Conflict")->end("Username already exists.");
                    return;
                }

                query = "INSERT INTO player (username, password) VALUES ($1, $2)";
                params = {username, password};

                txn.exec(query, params);
                txn.commit();
                std::string token = createJWT(username);
                res->end("Signup successful. Token: " + token);
            } else {
                pqxx::zview query = "SELECT password FROM player WHERE username = $1";
                pqxx::params params(username);
                
                pqxx::result r = txn.exec(query, params);
                if (r.empty() || r[0][0].as<std::string>() != password) {
                    res->writeStatus("401 Unauthorized")->end("Invalid credentials.");
                    return;
                }
                std::string token = createJWT(username);
                res->end("Login successful. Token: " + token);
            }
        } catch (const std::exception& e) {
            res->writeStatus("500 Internal Server Error")->end("Database error.");
        }
    });
}

std::string getTokenFromReq(uWS::HttpRequest * req) {
    std::string_view authHeader = req->getHeader("authorization");

    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") {
        // res->writeStatus("401 Unauthorized")->end("Missing or invalid Authorization header");
        return "";
    }

    std::string token = std::string(authHeader.substr(7));  // Skip "Bearer "

    return token;
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
        // res->writeStatus("401 Unauthorized")->end(std::string("Invalid token: ") + e.what());
    }

    return username;
}


void registerRoutes(uWS::SSLApp & app) {
    app.ws<PlayerData>("/new-game", {

        .open = [](auto* ws) {
            std::cout << "reg-game requested\n";
        },

        .message = [](auto* ws, std::string_view msg, uWS::OpCode code) {
            auto * playerData = ws->getUserData();

            std::stringstream ss{std::string(msg)};
            std::string msgType;
            ss >> msgType;

            if(msgType == "auth") {
                std::string token;
                ss >> token;

                std::string username = getUserNameFromToken(token);

                if(username.length() == 0) {
                    ws->send("Not authorised", uWS::OpCode::TEXT);
                    ws->close();
                }
                else {
                    ws->getUserData()->authDone = true;
                    playerData->name = username;
                    playerData->ws = ws;

                    if(regWaitingPlayer == nullptr) regWaitingPlayer = playerData;
                    else startGame(playerData);
                }
            }

            else if(msgType == "move") {
                if(!playerData->authDone) {
                    ws->send("Not authorised", uWS::OpCode::TEXT);
                    ws->close();
                    return;
                }

                auto & chess = ws->getUserData()->chess;

                try {
                    std::string move;
                    ss >> move;

                    chess->makeMove(move);

                    playerData->isMyTurn = false;
                    playerData->otherWs->getUserData()->isMyTurn = true;

                    // stop current player's timer
                    playerData->timerData->stopTimer();

                    playerData->ws->send("move:true", code);
                    playerData->otherWs->send("newmove:" + move, code);

                    // start opponent's timer
                    playerData->otherWs->getUserData()->timerData->startTimer();
                    
                    if(chess->isGameOver()) {
                        json resultJson;
                        resultJson["type"] = "result";

                        if(chess->isCheckMate(!playerData->isWhite)) {
                            resultJson["result"] = "win";
                            resultJson["reason"] = "Checkmate";
                            playerData->ws->send(resultJson.dump(), code);

                            resultJson["result"] = "lose";
                            resultJson["reason"] = "Checkmate";
                            playerData->otherWs->send(resultJson.dump(), code);
                        }
                        else {
                            resultJson["result"] = "draw";
                            
                            if(chess->isStalemate()) resultJson["reason"] = "Stalemate";
                            else if(chess->isDrawByInsufficientMaterial()) resultJson["reason"] = "Insufficient Material";
                            else if(chess->isDrawByRepitition()) resultJson["reason"] = "Repitition";
                            else if(chess->isDrawBy50MoveRule()) resultJson["reason"] = "50 Half Moves";

                            playerData->ws->send(resultJson.dump(), code);
                            playerData->otherWs->send(resultJson.dump(), code);
                        }

                        // close client sync timer
                        us_timer_close(playerData->syncTimer);
                    }

                } catch(std::exception& e) {
                    ws->send("move:false", code);
                    ws->send(std::string("Error:") + e.what(), code);
                }
            }
            else if(msgType == "chat") {
                if(!playerData->authDone) {
                    ws->send("Not authorised");
                    ws->close();
                    return;
                }

                ws->getUserData()->otherWs->send(msg);
            }
        }
    });

    app.ws<PlayerData>("/rand-game", {
        .upgrade = nullptr,

        .open = [](auto* ws) {
            std::cout << "Rand-game requested\n";
            auto * playerData = ws->getUserData();
            playerData->ws = ws;

            if(waitingPlayer == nullptr) waitingPlayer = playerData;
            else startGame(playerData);
        },

        .message = [](auto* ws, std::string_view msg, uWS::OpCode code) {
            auto * playerData = ws->getUserData();
            auto & chess = ws->getUserData()->chess;

            std::stringstream ss{std::string(msg)};

            std::string msgType;
            ss >> msgType;
            
            if(msgType == "move") {
                try {
                    std::string move;
                    ss >> move;

                    chess->makeMove(move);

                    playerData->isMyTurn = false;
                    playerData->otherWs->getUserData()->isMyTurn = true;

                    // stop current player's timer
                    playerData->timerData->stopTimer();

                    playerData->ws->send("move:true", code);
                    playerData->otherWs->send("newmove:" + move, code);

                    // start opponent's timer
                    playerData->otherWs->getUserData()->timerData->startTimer();
                    
                    if(chess->isGameOver()) {
                        json resultJson;
                        resultJson["type"] = "result";

                        if(chess->isCheckMate(!playerData->isWhite)) {
                            resultJson["result"] = "win";
                            resultJson["reason"] = "Checkmate";
                            playerData->ws->send(resultJson.dump(), code);

                            resultJson["result"] = "lose";
                            resultJson["reason"] = "Checkmate";
                            playerData->otherWs->send(resultJson.dump(), code);
                        }
                        else {
                            resultJson["result"] = "draw";
                            
                            if(chess->isStalemate()) resultJson["reason"] = "Stalemate";
                            else if(chess->isDrawByInsufficientMaterial()) resultJson["reason"] = "Insufficient Material";
                            else if(chess->isDrawByRepitition()) resultJson["reason"] = "Repitition";
                            else if(chess->isDrawBy50MoveRule()) resultJson["reason"] = "50 Half Moves";

                            playerData->ws->send(resultJson.dump(), code);
                            playerData->otherWs->send(resultJson.dump(), code);
                        }

                        // close client sync timer
                        us_timer_close(playerData->syncTimer);
                    }

                } catch(std::exception& e) {
                    ws->send("move:false", code);
                    ws->send(std::string("Error:") + e.what(), code);
                }
            }
            else if(msgType == "chat") {
                ws->getUserData()->otherWs->send(msg);
            }
        }
    });

    app.get("/home", [](auto * res, auto * req) {
        serveFile("views/index", ".html", res);
    });

    app.post("/signup", [](auto* res, auto* req) {
        res->onData([res](std::string_view data, bool last) mutable {
            if (last) {
                handleSignupOrLogin(res, data, pool, threadPool, true);
            }
        });
    });

    app.post("/login", [](auto* res, auto* req) {
        res->onData([res](std::string_view data, bool last) mutable {
            if (last) {
                handleSignupOrLogin(res, data, pool, threadPool, false);
            }
        });
    });
}


int main() {
    LC::compute();

    uv_loop.reset(uv_default_loop());

    if(!uv_loop) std::cout << "loop null\n";

    uWS::Loop::get(uv_loop.get());
    uWS::SSLApp app;
    
    registerRoutes(app);

    app.listen("0.0.0.0", 8080, [](auto * socket){
        if(!socket) std::cout << "Couldn't start the sever\n";
        else std::cout << "Server running at port 8080\n";
    });

    app.run();

    return 0;
}