cmake_minimum_required(VERSION 3.16)

project(publicShare C CXX)

option(MessageQueue_ON "Enable MessageQueue module" OFF)
option(Mysql_ON "Enable MySQL module" OFF)
option(Redis_ON "Enable Redis module" OFF)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(ALL_SRC_FILES)

# 可选模块
if(MessageQueue_ON)
    list(APPEND ALL_SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/MessageQueue.cpp)
    add_definitions(-DMESSAGEQUEUE_ENABLED=1)
endif()

if(Mysql_ON)
    list(APPEND ALL_SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/Mysql_Server.cpp)
    add_definitions(-DMYSQL_ENABLED=1)
endif()

if(Redis_ON)
    list(APPEND ALL_SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/Redis_Server.cpp)
    add_definitions(-DREDIS_ENABLED=1)
endif()

# 基础模块
list(APPEND ALL_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CriticalSectionLock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Sig_Manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SpinLock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RateLimiter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ThreadPool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FlexThreadPool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ResourcePool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Timer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroutineScheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroTask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroTimer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroConnection.cpp
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

SET(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

message(STATUS "=== publicShare 模块配置 ===")
message(STATUS "MessageQueue: ${MessageQueue_ON}")
message(STATUS "MySQL: ${Mysql_ON}")
message(STATUS "Redis: ${Redis_ON}")