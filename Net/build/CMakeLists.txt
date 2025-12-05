cmake_minimum_required(VERSION 3.16)

project(net C CXX)

include_directories(../include)
include_directories(../../publicShare/include)
include_directories(../../Log/include)

aux_source_directory(../src DIR_SRC)

option(USE_IO_URING "Use io_uring instead of epoll" OFF)

if(USE_IO_URING)
    message(STATUS "Building with io_uring support")
    add_definitions(-DIO_URING_ON=1)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -luring")
    list(FILTER DIR_SRC EXCLUDE REGEX ".*EpollCore\\.cpp$")
else()
    message(STATUS "Building without io_uring (using epoll)")
    list(FILTER DIR_SRC EXCLUDE REGEX ".*IOuringCore\\.cpp$")
endif()

ADD_LIBRARY(net SHARED ${DIR_SRC})

add_library(public SHARED IMPORTED)
set_target_properties(public PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}../bin/libpublic.so)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(public INTERFACE Threads::Threads)

set(CMAKE_BUILD_TYPE "Debug")
SET(LIBRARY_OUTPUT_PATH ../bin)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -fpermissive")