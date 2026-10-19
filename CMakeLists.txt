cmake_minimum_required(VERSION 3.28)
project(ledgefall_core VERSION 0.0.1 LANGUAGES CXX)

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
  GIT_TAG 953496fb410f80933067669eff409b6ad7f5845e)
# The engine's own tests and benchmark are its CI's job, not ours.
set(_ledgefall_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(fighter_physics)
set(BUILD_TESTING ${_ledgefall_build_testing})

add_library(ledgefall_core src/version.cpp)
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
endif()
