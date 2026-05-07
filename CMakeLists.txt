cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-shoutout-manager VERSION 1.0.0)

option(ENABLE_FRONTEND_API "Use the OBS frontend API" ON)
option(ENABLE_QT "Use Qt" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network WebSockets)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.cpp
          src/shoutout-dock.cpp
          src/twitch-auth.cpp
          src/twitch-api.cpp
          src/twitch-chat.cpp
          src/queue-manager.cpp
          src/token-store.cpp)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs
          OBS::obs-frontend-api
          Qt6::Core
          Qt6::Widgets
          Qt6::Network
          Qt6::WebSockets)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# TWITCH_CLIENT_ID must be supplied at configure time.
# e.g. cmake -B build -DTWITCH_CLIENT_ID=abc123
if(NOT DEFINED TWITCH_CLIENT_ID OR "${TWITCH_CLIENT_ID}" STREQUAL "")
  message(FATAL_ERROR
    "TWITCH_CLIENT_ID is required.\n"
    "Run: cmake -B build -DTWITCH_CLIENT_ID=<your_client_id>")
endif()

target_compile_definitions(${CMAKE_PROJECT_NAME}
  PRIVATE TWITCH_CLIENT_ID="${TWITCH_CLIENT_ID}")

set_target_properties_obs(${CMAKE_PROJECT_NAME} PROPERTIES UI_FOLDER "Frontend")
