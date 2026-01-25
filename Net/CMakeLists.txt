cmake_minimum_required(VERSION 3.16)

project(net C CXX)

option(USE_IO_URING "Use io_uring instead of epoll" ON)

SET(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin)


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../publicShare/include)

set(ALL_SRC_FILES)

list(APPEND ALL_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base64.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BaseNetWorkSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CRC32Helper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomTcpSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/IOuringCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/NetCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PureTCPClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PureWebSocketSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionListener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sha1_portable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/strHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TCPEndPoint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TcpEndPointListener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TCPTransportWarpper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebSocketClient.cpp
    )

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(USE_IO_URING)
        list(APPEND ALL_SRC_FILES  ${CMAKE_CURRENT_SOURCE_DIR}/src/IOuringCore.cpp)
        message(STATUS "Building with io_uring support")
    else()
        list(APPEND ALL_SRC_FILES  ${CMAKE_CURRENT_SOURCE_DIR}/src/EpollCore.cpp)
        message(STATUS "Building without io_uring (using epoll)")
    endif()
endif()

ADD_LIBRARY(net SHARED ${ALL_SRC_FILES})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(public INTERFACE Threads::Threads)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(USE_IO_URING)
        add_definitions(-DIO_URING_ON=1)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -luring")
    endif()

    if(NOT TARGET public)
        add_library(public SHARED IMPORTED)
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -fpermissive -fcoroutines")
    set_target_properties(public PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin/libpublic.so)
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