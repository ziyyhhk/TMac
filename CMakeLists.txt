cmake_minimum_required(VERSION 3.21)
project(TMac VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Framework-independent core. Builds and tests without Geode or Geometry Dash.
add_library(tmac_core STATIC
    src/core/Macro.cpp
    src/core/Gdr2Codec.cpp
    src/core/Recorder.cpp
    src/core/Player.cpp
    src/core/FixedStepClock.cpp
    src/core/Session.cpp
    src/core/MacroStore.cpp
    src/core/PanelView.cpp
)
target_include_directories(tmac_core PUBLIC include)
if(MSVC)
    target_compile_options(tmac_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(tmac_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(TMAC_BUILD_TESTS "Build the core unit tests" ON)
if(TMAC_BUILD_TESTS)
    enable_testing()
    add_executable(tmac_tests tests/test_core.cpp tests/test_phase2.cpp)
    target_link_libraries(tmac_tests PRIVATE tmac_core)
    add_test(NAME tmac_core COMMAND tmac_tests)
endif()

# Geode mod target. Only configured when the Geode SDK is provided through the
# GEODE_SDK environment variable.
# UNVERIFIED: the include path and setup_geode_mod() call follow the standard
# Geode mod template as remembered. Confirm them against the v5.11.1 SDK.
if(DEFINED ENV{GEODE_SDK})
    include($ENV{GEODE_SDK}/cmake/Geode.cmake)
    add_library(${PROJECT_NAME} SHARED
        src/geode/TMacRuntime.cpp
        src/geode/TMacMod.cpp
        src/geode/TMacPanel.cpp
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE tmac_core)
    setup_geode_mod()
endif()
