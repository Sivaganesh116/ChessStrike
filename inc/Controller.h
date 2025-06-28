#pragma once

#include "App.h"

#include "Data.h"

#include <string>

void serveFile(std::string fileName, std::string ext, uWS::HttpResponse<true>* res);

std::string createJWT(int userID, const std::string& username);

std::string getTokenFromReq(uWS::HttpRequest * req);

std::unordered_map<std::string, std::string> parseCookies(std::string_view cookieHeader);

std::pair<int, std::string> getUserNameIDFromToken(std::string token);

void matchPlayer(PlayerData* newPlayerData, bool randGame);

void gameMoveHandler(uWS::WebSocket<true, true, PlayerData>* ws, PlayerData* movedPlayerData, std::string move);

void randGameMoveHandler(PlayerData* movedPlayerData, std::string move);


void gameWSUpgradeHandler(uWS::HttpResponse<true> * res, uWS::HttpRequest * req, struct us_socket_context_t * context);

void gameWSMessageHandler(uWS::WebSocket<true, true, PlayerData>* ws, std::string_view msg, uWS::OpCode code);

void gameWSOpenHandler(uWS::WebSocket<true, true, PlayerData> * ws);

void randGameWSMessageHandler(uWS::WebSocket<true, true, PlayerData>* ws, std::string_view msg, uWS::OpCode code);

void gameWSCloseHandler(uWS::WebSocket<true, true, PlayerData>* ws, int code, std::string_view msg);

void watchWSUpgradeHandler(uWS::HttpResponse<true> * res, uWS::HttpRequest * req, struct us_socket_context_t * context);


void handleSignupOrLogin(uWS::HttpResponse<true>* res, std::string_view body, bool isSignup);

void httpResOnDataHandler(std::string_view data, bool isLast);

void getLiveGamesHandler(uWS::HttpResponse<true>* res, int numGames);

void gameHistoryHandler(uWS::HttpResponse<true>* res, int userID, int batchNumber, int numGames);

void getPlayerProfile(uWS::HttpResponse<true>* res, std::string username);

std::string getLiveGameOfUser(int userID);

void getGameHandler(uWS::HttpResponse<true>* res, int gameID, int reqUserID);
