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
  GIT_TAG eaff550c6c3c23f7d79c2173d8c4e6ca41c6b76e)
# The engine's own tests and benchmark are its CI's job, not ours.
set(_ledgefall_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(fighter_physics)
set(BUILD_TESTING ${_ledgefall_build_testing})

add_library(ledgefall_core src/version.cpp src/input.cpp src/stage.cpp src/match.cpp src/state_io.cpp src/bot.cpp src/capi.cpp src/characters.cpp src/moves.cpp src/contact.cpp src/objects.cpp src/hazards.cpp src/replay.cpp src/oneway.cpp)
add_library(ledgefall::ledgefall_core ALIAS ledgefall_core)
target_link_libraries(ledgefall_core PUBLIC fp::fighter_physics)
target_include_directories(ledgefall_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(ledgefall_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# netplay-kit, pinned by commit (docs/decisions/0002-netplay-kit-pin.md). Its targets are EXCLUDE_FROM_ALL;
# what we build of it is below.
option(LEDGEFALL_BUILD_SERVER "Build the netplay-kit adapter and the game server" ON)
option(LEDGEFALL_BUILD_NETCLIENT "Build the online client library (for the app)" ON)
if(LEDGEFALL_BUILD_SERVER OR LEDGEFALL_BUILD_NETCLIENT)
  FetchContent_Declare(netplay_kit
    GIT_REPOSITORY https://github.com/scshafe/netplay-kit.git
    GIT_TAG 75440f27a86081fde5c800aecd3b0597816c390f
    EXCLUDE_FROM_ALL)
  set(NPK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(NPK_SANITIZE OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(netplay_kit)
endif()

if(LEDGEFALL_BUILD_NETCLIENT)
  # The app's side: only netplay-kit's transport and sessions, compiled here at the app's macOS floor. Its
  # JSON, HTTP and lobby code (which uses std::from_chars for doubles, unavailable below macOS 26) is not
  # in it: the app talks to the lobby with URLSession. The sources are found by directory, so a bump of the
  # pin needs no edit here unless netplay-kit moves them.
  file(GLOB NPK_CLIENT_SOURCES CONFIGURE_DEPENDS
    ${netplay_kit_SOURCE_DIR}/src/session/*.cpp
    ${netplay_kit_SOURCE_DIR}/src/transport/*.cpp
    ${netplay_kit_SOURCE_DIR}/src/util/*.cpp
    ${netplay_kit_SOURCE_DIR}/src/version.cpp)
  add_library(ledgefall_netclient STATIC ${NPK_CLIENT_SOURCES} src/netplay.cpp src/online.cpp src/capi_net.cpp)
  add_library(ledgefall::ledgefall_netclient ALIAS ledgefall_netclient)
  target_include_directories(ledgefall_netclient PUBLIC ${netplay_kit_SOURCE_DIR}/include)
  target_include_directories(ledgefall_netclient PRIVATE ${netplay_kit_SOURCE_DIR}/src)
  target_link_libraries(ledgefall_netclient PUBLIC ledgefall::ledgefall_core)
endif()

if(LEDGEFALL_BUILD_SERVER)
  add_library(ledgefall_netplay src/netplay.cpp src/netplay_options.cpp)
  add_library(ledgefall::ledgefall_netplay ALIAS ledgefall_netplay)
  target_link_libraries(ledgefall_netplay PUBLIC ledgefall::ledgefall_core npk::netplay_kit)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ledgefall_netplay PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()

  add_executable(ledgefall_bot server/bot.cpp)
  target_link_libraries(ledgefall_bot PRIVATE ledgefall::ledgefall_netplay)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ledgefall_bot PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()

  add_executable(ledgefall_server server/main.cpp)
  target_link_libraries(ledgefall_server PRIVATE ledgefall::ledgefall_netplay)
  # The lobby is netplay-kit's; the end-to-end test needs it built (it is EXCLUDE_FROM_ALL there).
  add_custom_target(ledgefall_lobby_for_tests ALL DEPENDS npk_lobby)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ledgefall_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
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

  foreach(name scenario determinism_trace bot capi kit glim pip slab fuse fern_dab ferro hazard team replay oneway belts straddle)
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

  if(LEDGEFALL_BUILD_NETCLIENT)
    add_executable(ledgefall_online_test tests/online_test.cpp)
    target_link_libraries(ledgefall_online_test PRIVATE ledgefall::ledgefall_netclient)
    add_test(NAME ledgefall_online COMMAND ledgefall_online_test)
  endif()

  if(LEDGEFALL_BUILD_SERVER)
    add_executable(ledgefall_netplay_test tests/netplay_test.cpp)
    target_link_libraries(ledgefall_netplay_test PRIVATE ledgefall::ledgefall_netplay)
    add_test(NAME ledgefall_netplay COMMAND ledgefall_netplay_test)
    # The real executables: a lobby, the game server and two bots playing a match to its end.
    add_test(NAME ledgefall_e2e COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e ${CMAKE_BINARY_DIR})
  endif()

  add_test(NAME ledgefall_core_determinism_guards
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-determinism-guards)
endif()
