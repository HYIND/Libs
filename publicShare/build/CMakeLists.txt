cmake_minimum_required(VERSION 3.16)

project(publicShare C CXX)

option(MessageQueue_ON "Enable MessageQueue module" OFF)
option(Mysql_ON "Enable MySQL module" OFF)
option(Redis_ON "Enable Redis module" OFF)

include_directories(../include)

set(ALL_SRC_FILES)

# 可选模块
if(MessageQueue_ON)
    list(APPEND ALL_SRC_FILES ../src/MessageQueue.cpp)
    add_definitions(-DMESSAGEQUEUE_ENABLED=1)
endif()

if(Mysql_ON)
    list(APPEND ALL_SRC_FILES ../src/Mysql_Server.cpp)
    add_definitions(-DMYSQL_ENABLED=1)
endif()

if(Redis_ON)
    list(APPEND ALL_SRC_FILES ../src/Redis_Server.cpp)
    add_definitions(-DREDIS_ENABLED=1)
endif()

# 基础模块
list(APPEND ALL_SRC_FILES
    ../src/Config.cpp
    ../src/CriticalSectionLock.cpp
    ../src/Sig_Manager.cpp
    ../src/SpinLock.cpp
    ../src/RateLimiter.cpp
    ../src/ThreadPool.cpp
    ../src/ResourcePool.cpp
    ../src/Timer.cpp
    ../src/Coroutine.cpp
)

ADD_LIBRARY(public SHARED ${ALL_SRC_FILES})

IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt")
    IF(Mysql_ON)
        target_link_libraries(public libmysqlclient.so)
    ENDIF()
ENDIF()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")

set(CMAKE_BUILD_TYPE "Debug")

SET(LIBRARY_OUTPUT_PATH ../bin)

message(STATUS "=== publicShare 模块配置 ===")
message(STATUS "MessageQueue: ${MessageQueue_ON}")
message(STATUS "MySQL: ${Mysql_ON}")
message(STATUS "Redis: ${Redis_ON}")