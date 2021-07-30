cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)

project(datetime VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall")
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")

include(fmt)

add_library(datetime STATIC ${PROJECT_SOURCE_DIR}/src/datetime.cc)
target_include_directories(datetime PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(datetime PRIVATE fmt)

add_library(datetime::datetime ALIAS datetime)
