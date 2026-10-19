cmake_minimum_required(VERSION 3.28)
project(ledgefall_core VERSION 0.0.1 LANGUAGES C CXX)

# Ledgefall: the game's shared C++ core (rules, simulation glue) used by the macOS client and the server.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# fighter-physics, at a pinned commit. Bump the pin deliberately in its own PR
# (docs/decisions/0001-engine-pin.md), never by following a branch.
include(FetchContent)
FetchContent_Declare(fighter_physics
  GIT_REPOSITORY https://github.com/scshafe/fighter-physics.git
  GIT_TAG 546316bc15c625da43131c918ecb0975ccf4f325)
# The engine's own tests and benchmark are its CI's job, not ours.
set(_ledgefall_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(fighter_physics)
set(BUILD_TESTING ${_ledgefall_build_testing})

add_library(ledgefall_core src/version.cpp src/input.cpp src/stage.cpp src/match.cpp src/state_io.cpp src/bot.cpp src/capi.cpp src/characters.cpp src/moves.cpp src/contact.cpp src/objects.cpp)
add_library(ledgefall::ledgefall_core ALIAS ledgefall_core)
target_link_libraries(ledgefall_core PUBLIC fp::fighter_physics)
target_include_directories(ledgefall_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(ledgefall_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(ledgefall_core_smoke_test tests/smoke_test.cpp)
  target_link_libraries(ledgefall_core_smoke_test PRIVATE ledgefall::ledgefall_core)
  add_test(NAME ledgefall_core_smoke COMMAND ledgefall_core_smoke_test)

  add_executable(ledgefall_core_engine_link_test tests/engine_link_test.cpp)
  target_link_libraries(ledgefall_core_engine_link_test PRIVATE ledgefall::ledgefall_core)
  add_test(NAME ledgefall_core_engine_link COMMAND ledgefall_core_engine_link_test)

  add_executable(ledgefall_core_match_test tests/match_test.cpp)
  target_link_libraries(ledgefall_core_match_test PRIVATE ledgefall::ledgefall_core)
  add_test(NAME ledgefall_core_match COMMAND ledgefall_core_match_test)

  foreach(name scenario determinism_trace bot capi kit glim pip)
    add_executable(ledgefall_core_${name}_test tests/${name}_test.cpp)
    target_link_libraries(ledgefall_core_${name}_test PRIVATE ledgefall::ledgefall_core)
    target_compile_definitions(ledgefall_core_${name}_test PRIVATE
      LEDGEFALL_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
    add_test(NAME ledgefall_core_${name} COMMAND ledgefall_core_${name}_test)
  endforeach()

  # The C header must be valid C: the Swift app talks to the core through it.
  add_executable(ledgefall_core_capi_c_test tests/capi_c_test.c)
  target_link_libraries(ledgefall_core_capi_c_test PRIVATE ledgefall::ledgefall_core)
  target_compile_options(ledgefall_core_capi_c_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME ledgefall_core_capi_c COMMAND ledgefall_core_capi_c_test)

  add_test(NAME ledgefall_core_determinism_guards
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-determinism-guards)
endif()
