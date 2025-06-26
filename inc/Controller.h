#pragma once

#include "App.h"

#include "Data.h"

#include <string>



std::unordered_map<std::string, std::string> parseCookies(std::string_view cookieHeader);

std::pair<int, std::string> getUserNameIDFromToken(std::string token);

void matchPlayer(PlayerData* newPlayerData, bool randGame);

void gameMoveHandler(PlayerData* movedPlayerData, std::string move);

void randGameMoveHandler(PlayerData* movedPlayerData, std::string move);


void gameWSUpgradeHandler(uWS::HttpResponse<true> * res, uWS::HttpRequest * req, struct us_socket_context_t * context);

void gameWSMessageHandler(uWS::WebSocket<true, true, PlayerData>* ws, std::string_view msg, uWS::OpCode code);

void gameWSOpenHandler(uWS::WebSocket<true, true, PlayerData> * ws);

void randGameWSMessageHandler(uWS::WebSocket<true, true, PlayerData>* ws, std::string_view msg, uWS::OpCode code);

void gameWSCloseHandler(uWS::WebSocket<true, true, PlayerData>* ws, int code, std::string_view msg);
