cmake_minimum_required(VERSION 3.16)

project(net C CXX)

# set(CMAKE_BUILD_TYPE "Debug")

option(EchoServerSimple_ON "Enable EchoServerSimple" ON)
option(EchoClientSimple_ON "Enable EchoClientSimple" ON)
option(CoroutineSimple_ON "Enable CoroutineSimple" ON)
option(ThreadPoolSimple_ON "Enable ThreadPoolSimple" ON)
option(TimerSimple_ON "Enable TimerSimple" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Net/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../publicShare/include)

IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -pthread")
ENDIF()

set(ALL_LIBS)

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(PUBLIC_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin/public.lib)
    set(NET_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin/net.lib)

    find_library(NET_LIB net PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../bin NO_DEFAULT_PATH)
    find_library(PUBLIC_LIB public PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../bin NO_DEFAULT_PATH)

    list(APPEND ALL_LIBS ${NET_LIB})
    list(APPEND ALL_LIBS ${PUBLIC_LIB})

elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(PUBLIC_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin/libpublic.so)
    set(NET_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin/libnet.so)

    # 检查库文件是否存在（调试用）
    message(STATUS "Checking libpublic.so at: ${PUBLIC_LIB_PATH}")
    message(STATUS "Checking libnet.so at: ${NET_LIB_PATH}/../bin/libnet.so")

    # 定义导入库（绝对路径 + 检查存在性）
    find_library(NET_LIB net PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../bin NO_DEFAULT_PATH)
    find_library(PUBLIC_LIB public PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../bin NO_DEFAULT_PATH)
    find_library(FMT_LIB fmt PATHS /usr/lib /usr/local/lib)
    find_library(URING_LIB uring PATHS /usr/lib /usr/local/lib)

    if(NOT NET_LIB)
        message(WARNING "libnet.so not found in ${${CMAKE_CURRENT_SOURCE_DIR}}/../bin")
    endif()

    if(NOT PUBLIC_LIB)
        message(WARNING "libpublic.so not found in ${${CMAKE_CURRENT_SOURCE_DIR}}/../bin")
    endif()

    if(NOT FMT_LIB)
        message("fmt library not found!")
    endif()

    if(NOT URING_LIB)
        message(WARNING "uring library not found!")
    endif()

    list(APPEND ALL_LIBS ${NET_LIB})
    list(APPEND ALL_LIBS ${PUBLIC_LIB})

    if(URING_LIB)
        list(APPEND ALL_LIBS ${URING_LIB})
    endif()
    if(FMT_LIB)
        list(APPEND ALL_LIBS ${URING_LIB})
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -fpermissive -fcoroutines -g")
endif()

if(EchoServerSimple_ON)
    add_executable(EchoServerSimple ${CMAKE_CURRENT_SOURCE_DIR}/EchoServerSimple.cpp)
    target_link_libraries(EchoServerSimple ${ALL_LIBS})
endif()

if(EchoClientSimple_ON)
    add_executable(EchoClientSimple ${CMAKE_CURRENT_SOURCE_DIR}/EchoClientSimple.cpp)
    target_link_libraries(EchoClientSimple ${ALL_LIBS})
endif()

if(CoroutineSimple_ON)
    add_executable(CoroutineSimple ${CMAKE_CURRENT_SOURCE_DIR}/CoroutineSimple.cpp)
    target_link_libraries(CoroutineSimple ${ALL_LIBS})
endif()

if(ThreadPoolSimple_ON)
add_executable(ThreadPoolSimple ${CMAKE_CURRENT_SOURCE_DIR}/ThreadPoolSimple.cpp)
target_link_libraries(ThreadPoolSimple ${ALL_LIBS})
endif()

if(TimerSimple_ON)
    add_executable(TimerSimple ${CMAKE_CURRENT_SOURCE_DIR}/TimerSimple.cpp)
    target_link_libraries(TimerSimple ${ALL_LIBS})
endif()

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address  -fsanitize-recover=address -fno-stack-protector -fno-omit-frame-pointer -fno-var-tracking")
# target_link_libraries(EchoServerSimple asan)
# target_link_libraries(EchoClientSimple asan)
