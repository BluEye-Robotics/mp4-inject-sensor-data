# CMakeLists.txt
cmake_minimum_required (VERSION 3.5.1)
project (mp4-inject-sensor-data)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O2 -fPIC -g -Wno-write-strings")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0>=1.4)

include_directories(${GST_INCLUDE_DIRS})
include_directories(external)

link_libraries(${GST_LIBRARIES})

file(GLOB SOURCES src/*.c* external/*/*.c)

add_executable(mp4-inject-sensor-data ${SOURCES})

target_link_libraries(mp4-inject-sensor-data ${GST_LIBRARIES})
