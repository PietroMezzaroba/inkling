cmake_minimum_required(VERSION 3.16)
project(inkling_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FREEINK_UI "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI")
set(INKLING_CORE "${REPO_ROOT}/lib/InklingCore/src")

file(GLOB_RECURSE INKLING_CORE_SOURCES CONFIGURE_DEPENDS "${INKLING_CORE}/*.cpp")

add_executable(inkling-sim
  main.cpp
  PngWriter.cpp
  ${FREEINK_UI}/src/FreeInkUI.cpp
  ${INKLING_CORE_SOURCES}
)
target_include_directories(inkling-sim PRIVATE ${FREEINK_UI}/include ${INKLING_CORE})
target_compile_options(inkling-sim PRIVATE -Wall -Wextra)
