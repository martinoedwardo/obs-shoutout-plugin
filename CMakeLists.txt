cmake_minimum_required(VERSION 3.20)
project(obs-shoutout-plugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define paths based on the 'obs-sdk' folder created in the YAML
set(OBS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/obs-sdk/include")
set(OBS_LIB_DIR "${CMAKE_SOURCE_DIR}/obs-sdk/bin/64bit")

# Ensure the compiler can find obs.h
include_directories(${OBS_INCLUDE_DIR})

# Define your source files
set(PLUGIN_SOURCES
    src/plugin-main.cpp
    src/twitch-chat.cpp
    src/queue-manager.cpp
)

add_library(${PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

# Tell the linker to find obs.lib in the bin folder
target_link_libraries(${PROJECT_NAME}
    "${CMAKE_SOURCE_DIR}/obs-sdk/build/libobs/Release/libobs.lib"
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "obs-shoutout-plugin"
)