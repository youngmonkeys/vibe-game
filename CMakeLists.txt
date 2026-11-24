cmake_minimum_required(VERSION 3.25)
project(VibeGame VERSION 0.1.0 LANGUAGES C CXX)

include(cmake/VibeGame.cmake)

vibe_add_game(VibeGame
    NAME VibeGame
    ORG youngmonkeys
    ID vibegame
    BUNDLE_ID org.youngmonkeys.vibegame)

if(VIBE_ANDROID OR VIBE_WINDOWS OR VIBE_IOS)
    return()
endif()
add_subdirectory(tests/node)
