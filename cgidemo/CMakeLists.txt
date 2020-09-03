cmake_minimum_required(VERSION 2.8)
project(aiboxdemo)
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fPIC")
file(GLOB SRCS  "*.cc")
include_directories(
    ${PROJECT_SOURCE_DIR}
    rapidjson/include/
)
link_directories(${PROJECT_SOURCE_DIR}/libs/)
add_executable(aiboxdemo  ${SRCS})
target_link_libraries(aiboxdemo fcgi)
