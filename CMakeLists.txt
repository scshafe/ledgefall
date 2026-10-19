cmake_minimum_required(VERSION 3.28)
project(ledgefall_core VERSION 0.0.1 LANGUAGES CXX)

# Ledgefall: the game's shared C++ core (rules, simulation glue) used by the macOS client and the server.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(ledgefall_core src/version.cpp)
add_library(ledgefall::ledgefall_core ALIAS ledgefall_core)
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
endif()
