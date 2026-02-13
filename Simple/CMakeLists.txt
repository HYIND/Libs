cmake_minimum_required(VERSION 3.16)

project(net C CXX)

# set(CMAKE_BUILD_TYPE "Debug")

option(EchoServerSimple_ON "Enable EchoServerSimple" ON)
option(EchoClientSimple_ON "Enable EchoClientSimple" ON)
option(CoroutineSimple_ON "Enable CoroutineSimple" ON)
option(ThreadPoolSimple_ON "Enable ThreadPoolSimple" ON)
option(TimerSimple_ON "Enable TimerSimple" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Net/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../publicShare/include)

IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -pthread")
ENDIF()

if(CMAKE_SYSTEM_NAME MATCHES "Windows")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(PUBLIC_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../bin/public_d.lib)
        set(NET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../bin/net_d.lib)
    else()
        set(PUBLIC_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../bin/public.lib)
        set(NET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../bin/net.lib)
    endif()

elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(PUBLIC_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../bin/libpublic_d.so)
        set(NET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../bin/libnet_d.so)
    else()
        set(PUBLIC_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../bin/libpublic.so)
        set(NET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../bin/libnet.so)
    endif()

    find_library(URING_LIB uring PATHS /usr/lib /usr/local/lib)

    if(NOT URING_LIB)
        message(WARNING "uring library not found!")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -fpermissive -fcoroutines -g")
endif()



if(EchoServerSimple_ON)
    add_executable(EchoServerSimple ${CMAKE_CURRENT_SOURCE_DIR}/EchoServerSimple.cpp)
    target_link_libraries(EchoServerSimple ${NET_LIB} ${PUBLIC_LIB})

    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        if(URING_LIB)
            target_link_libraries(EchoServerSimple ${URING_LIB})
        endif()
    endif()

endif()

if(EchoClientSimple_ON)
    add_executable(EchoClientSimple ${CMAKE_CURRENT_SOURCE_DIR}/EchoClientSimple.cpp)
    target_link_libraries(EchoClientSimple ${NET_LIB} ${PUBLIC_LIB})

    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        if(URING_LIB)
            target_link_libraries(EchoClientSimple ${URING_LIB})
        endif()
    endif()

endif()

if(CoroutineSimple_ON)
    add_executable(CoroutineSimple ${CMAKE_CURRENT_SOURCE_DIR}/CoroutineSimple.cpp)
    target_link_libraries(CoroutineSimple ${NET_LIB} ${PUBLIC_LIB})

    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        if(URING_LIB)
            target_link_libraries(CoroutineSimple ${URING_LIB})
        endif()
    endif()

endif()

if(ThreadPoolSimple_ON)
    add_executable(ThreadPoolSimple ${CMAKE_CURRENT_SOURCE_DIR}/ThreadPoolSimple.cpp)
    target_link_libraries(ThreadPoolSimple ${NET_LIB} ${PUBLIC_LIB})

    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        if(URING_LIB)
            target_link_libraries(ThreadPoolSimple ${URING_LIB})
        endif()
    endif()

endif()

if(TimerSimple_ON)
    add_executable(TimerSimple ${CMAKE_CURRENT_SOURCE_DIR}/TimerSimple.cpp)
    target_link_libraries(TimerSimple ${NET_LIB} ${PUBLIC_LIB})

    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        if(URING_LIB)
            target_link_libraries(TimerSimple ${URING_LIB})
        endif()
    endif()

endif()

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address  -fsanitize-recover=address -fno-stack-protector -fno-omit-frame-pointer -fno-var-tracking")
# target_link_libraries(EchoServerSimple asan)
# target_link_libraries(EchoClientSimple asan)
