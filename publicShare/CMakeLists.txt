cmake_minimum_required(VERSION 3.16)

project(publicShare C CXX)

option(MessageQueue_ON "Enable MessageQueue module" OFF)
option(Mysql_ON "Enable MySQL module" OFF)
option(Redis_ON "Enable Redis module" OFF)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(ALL_SRC_FILES)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroTask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroConnection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Timer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TimeWheel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HighPrecisionTimer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroTimer.cpp
)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    list(APPEND ALL_SRC_FILES  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroutineScheduler_Linux.cpp)
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
    list(APPEND ALL_SRC_FILES  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoroutineScheduler_Win.cpp)
endif()

ADD_LIBRARY(public SHARED ${ALL_SRC_FILES})

IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
    IF(Mysql_ON)
        target_link_libraries(public libmysqlclient.so)
    ENDIF()
ENDIF()

if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

SET(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    message(STATUS "=== publicShare 模块配置 ===")
    message(STATUS "MessageQueue: ${MessageQueue_ON}")
    message(STATUS "MySQL: ${Mysql_ON}")
    message(STATUS "Redis: ${Redis_ON}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug 配置添加 _d 后缀
    if(WIN32)
        # Windows/Visual Studio（多配置）
        set_target_properties(public PROPERTIES
            DEBUG_POSTFIX "_d"
            # 确保所有输出目录一致
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin
        )
    else()
        # Linux/GCC（单配置）
        set_target_properties(public PROPERTIES
            OUTPUT_NAME "public_d"  # 直接改名为 public_d
        )
    endif()
else()
    # Release 配置保持原名
    set_target_properties(public PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin
    )
endif()

target_compile_definitions(public PRIVATE PUBLICSHARE_EXPORTS)
