cmake_minimum_required(VERSION 3.21)
project(TMac VERSION 1.2.0)

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
    src/core/ToggleController.cpp
)
target_include_directories(tmac_core PUBLIC include)
if(MSVC)
    target_compile_options(tmac_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(tmac_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests default on for plain builds and off for the Geode mod build.
if(DEFINED ENV{GEODE_SDK})
    set(_tmac_tests_default OFF)
else()
    set(_tmac_tests_default ON)
endif()
option(TMAC_BUILD_TESTS "Build the core unit tests" ${_tmac_tests_default})
if(TMAC_BUILD_TESTS)
    enable_testing()
    add_executable(tmac_tests tests/test_core.cpp tests/test_phase2.cpp)
    target_link_libraries(tmac_tests PRIVATE tmac_core)
    add_test(NAME tmac_core COMMAND tmac_tests)
endif()

# Geode mod target.
# Integration follows the official Geode example mod: the SDK directory is added
# as a subdirectory, the mod target is created, and setup_geode_mod() is called
# with the target name. The SDK must be installed by the official tooling
# (`geode sdk install`, or geode-sdk/build-geode-mod in CI). Do not clone it by hand.
if(DEFINED ENV{GEODE_SDK})
    if(NOT EXISTS "$ENV{GEODE_SDK}/CMakeLists.txt")
        message(FATAL_ERROR
            "GEODE_SDK is '$ENV{GEODE_SDK}', but that folder has no CMakeLists.txt. "
            "Install the Geode SDK with the official tooling (geode sdk install, or "
            "geode-sdk/build-geode-mod in GitHub Actions) and point GEODE_SDK at it.")
    endif()
    message(STATUS "Using Geode SDK at $ENV{GEODE_SDK}")
    add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

    add_library(${PROJECT_NAME} SHARED
        src/geode/TMacRuntime.cpp
        src/geode/TMacMod.cpp
        src/geode/TMacPanel.cpp
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE tmac_core)
    setup_geode_mod(${PROJECT_NAME})
endif()
