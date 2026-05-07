cmake_minimum_required(VERSION 3.20)
project(obs-shoutout-plugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# Set the path to the extracted dependencies
set(OBS_SDK_PATH "${CMAKE_SOURCE_DIR}/obs-sdk")

# Include paths for libobs headers
include_directories("${OBS_SDK_PATH}/include")

set(PLUGIN_SOURCES
    src/plugin-main.cpp
    src/twitch-chat.cpp
    src/queue-manager.cpp
)

add_library(${PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

# Link against the libobs.lib file found in the SDK
target_link_libraries(${PROJECT_NAME}
    "${OBS_SDK_PATH}/lib/libobs.lib"
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "obs-shoutout-plugin"
)