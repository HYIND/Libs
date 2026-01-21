cmake_minimum_required(VERSION 3.16)

project(net C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../bin)

include_directories(../../Net/include)
include_directories(../../publicShare/include)

IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -pthread")
ENDIF()

# 检查库文件是否存在（调试用）
message(STATUS "Checking libnet.so at: ${CMAKE_SOURCE_DIR}/../bin/libnet.so")
message(STATUS "Checking libpublic.so at: ${CMAKE_SOURCE_DIR}/../bin/libpublic.so")

# 定义导入库（绝对路径 + 检查存在性）
find_library(NET_LIB net PATHS ${CMAKE_SOURCE_DIR}/../bin NO_DEFAULT_PATH)
find_library(PUBLIC_LIB public PATHS ${CMAKE_SOURCE_DIR}/../bin NO_DEFAULT_PATH)
find_library(FMT_LIB fmt PATHS /usr/lib /usr/local/lib)
find_library(URING_LIB uring PATHS /usr/lib /usr/local/lib)

if(NOT NET_LIB)
    message(FATAL_ERROR "libnet.so not found in ${CMAKE_SOURCE_DIR}/../bin")
endif()

if(NOT PUBLIC_LIB)
    message(FATAL_ERROR "libpublic.so not found in ${CMAKE_SOURCE_DIR}/../bin")
endif()

if(NOT FMT_LIB)
    message(FATAL_ERROR "fmt library not found!")
endif()

if(NOT URING_LIB)
    message(FATAL_ERROR "uring library not found!")
endif()

add_executable(ServerSimple ../serverSimple.cpp)

target_link_libraries(ServerSimple ${FMT_LIB})
target_link_libraries(ServerSimple ${NET_LIB})
target_link_libraries(ServerSimple ${PUBLIC_LIB})

if(URING_LIB)
    target_link_libraries(ServerSimple ${URING_LIB})
endif()

add_executable(ClientSimple ../clientSimple.cpp)

target_link_libraries(ClientSimple ${FMT_LIB})
target_link_libraries(ClientSimple ${NET_LIB})
target_link_libraries(ClientSimple ${PUBLIC_LIB})

if(URING_LIB)
    target_link_libraries(ClientSimple ${URING_LIB})
endif()


set(CMAKE_BUILD_TYPE "Debug")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -fpermissive -fcoroutines -g")

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address  -fsanitize-recover=address -fno-stack-protector -fno-omit-frame-pointer -fno-var-tracking")
# target_link_libraries(ServerSimple asan)
# target_link_libraries(ClientSimple asan)
