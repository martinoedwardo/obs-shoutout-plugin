cmake_minimum_required(VERSION 3.20)

project(obs-shoutout-plugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OBS (GitHub Actions will use the OBS Studio source/SDK)
find_package(libobs REQUIRED)

# Include directories
include_directories(${LIBOBS_INCLUDE_DIRS} src)

# Define the plugin source files
set(PLUGIN_SOURCES
    src/plugin-main.cpp
    src/twitch-chat.cpp
    src/queue-manager.cpp
)

# Create the shared library (.dll)
add_library(${PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

target_link_libraries(${PROJECT_NAME}
    libobs
)

# Set output names
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "obs-shoutout-plugin"
)