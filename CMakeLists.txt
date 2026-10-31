cmake_minimum_required(VERSION 3.20)
project(minic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(MINIC_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined")

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
if(MINIC_SANITIZE)
  add_compile_options(-fsanitize=${MINIC_SANITIZE} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${MINIC_SANITIZE})
endif()

add_library(minic-core STATIC
  src/checker.cpp
  src/codegen.cpp
  src/lexer.cpp
  src/optimizer.cpp
  src/parser.cpp
)
target_include_directories(minic-core PUBLIC src)

add_executable(minic src/main.cpp)
target_link_libraries(minic PRIVATE minic-core)

enable_testing()
find_package(GTest REQUIRED)
add_executable(minic-tests tests/unit_tests.cpp)
target_link_libraries(minic-tests PRIVATE minic-core GTest::gtest_main)
add_test(NAME unit COMMAND minic-tests)
