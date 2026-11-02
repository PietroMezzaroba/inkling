cmake_minimum_required(VERSION 3.16)
project(inkling_sim C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FREEINK_UI "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI")
set(FREEINK_BOOK "${REPO_ROOT}/freeink-sdk/libs/book/FreeInkBook")
set(INKLING_CORE "${REPO_ROOT}/lib/InklingCore/src")

# FreeInkBook: vendored C deps + the C++ engine, with the same small-RAM
# profile as the ESP32-C3 build so arena peaks match the device.
set(BOOK_INCLUDES
  ${FREEINK_BOOK}/include
  ${FREEINK_BOOK}/third_party/expat
  ${FREEINK_BOOK}/third_party/miniz
  ${FREEINK_BOOK}/third_party/libunibreak
  ${FREEINK_BOOK}/third_party/pngle
  ${FREEINK_BOOK}/third_party/tjpgd
  ${FREEINK_BOOK}/third_party/stb)
file(GLOB BOOK_VENDOR_SOURCES "${FREEINK_BOOK}/src/vendor/*.c")
file(GLOB_RECURSE BOOK_SOURCES CONFIGURE_DEPENDS "${FREEINK_BOOK}/src/*.cpp")
add_library(freeinkbook STATIC ${BOOK_VENDOR_SOURCES} ${BOOK_SOURCES})
target_include_directories(freeinkbook PUBLIC ${BOOK_INCLUDES})
target_compile_definitions(freeinkbook PUBLIC FREEINK_BOOK_SMALL=1)
set_source_files_properties(${BOOK_VENDOR_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

file(GLOB_RECURSE INKLING_CORE_SOURCES CONFIGURE_DEPENDS "${INKLING_CORE}/*.cpp")

add_executable(inkling-sim
  main.cpp
  PngWriter.cpp
  HostStorage.cpp
  ${FREEINK_UI}/src/FreeInkUI.cpp
  ${INKLING_CORE_SOURCES}
)
target_include_directories(inkling-sim PRIVATE ${FREEINK_UI}/include ${INKLING_CORE})
target_link_libraries(inkling-sim PRIVATE freeinkbook)
target_compile_definitions(inkling-sim PRIVATE INKLING_FONT_DIR="${REPO_ROOT}/assets/fonts")
target_compile_options(inkling-sim PRIVATE -Wall -Wextra -include ${CMAKE_CURRENT_SOURCE_DIR}/SimLog.h)
