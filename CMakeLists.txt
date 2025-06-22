cmake_minimum_required(VERSION 3.15)
project(ChessStrike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

add_executable(chess_strike src/main.cpp)

# Set compiler flags
# target_compile_options(chess_strike PRIVATE
#     $<$<CONFIG:Release>:-O3 -march=native>
#     $<$<NOT:$<CONFIG:Release>>:-g>
#     -Wall -Wextra -Wpedantic
# )

find_library(LegalChess_LIB 
    NAMES LegalChess
    PATHS ${CMAKE_SOURCE_DIR}/external/LegalChess/build/
    NO_DEFAULT_PATH
)

set(uSockets_LIB ${CMAKE_SOURCE_DIR}/external/uSockets/uSockets.a)

find_library(uv_LIB 
    NAMES uv
    PATHS ${CMAKE_SOURCE_DIR}/external/libuv/lib/
    NO_DEFAULT_PATH
)

if(NOT LegalChess_LIB)
    message("Fatal Error. LegalChess is not found.")
endif()

if(NOT uSockets_LIB)
    message("Fatal Error. uSockets is not found.")
endif()

if(NOT uv_LIB)
    message("Fatal Error. uv is not found.")
endif()

target_include_directories(chess_strike PRIVATE
    ${CMAKE_SOURCE_DIR}/inc/
    ${CMAKE_SOURCE_DIR}/external/LegalChess/inc
    ${CMAKE_SOURCE_DIR}/external/uWebSockets/src
    ${CMAKE_SOURCE_DIR}/external/uSockets/src
    ${CMAKE_SOURCE_DIR}/external/libuv/include
    ${CMAKE_SOURCE_DIR}/external/jwt-cpp/
    ${CMAKE_SOURCE_DIR}/external/picojson/
    ${CMAKE_SOURCE_DIR}/external/
)

# Add Conan-generated CMake modules
find_package(libpqxx REQUIRED)
find_package(nlohmann_json REQUIRED)
# find_package(jwt-cpp REQUIRED)
# find_package(libuv REQUIRED)

# in system
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# Link dependencies
target_link_libraries(chess_strike PRIVATE
    libpqxx::pqxx
    nlohmann_json::nlohmann_json
    # uv
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    ${LegalChess_LIB}
    ${uSockets_LIB}
    ${uv_LIB}
)
