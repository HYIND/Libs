cmake_minimum_required (VERSION 3.16)

project(net C CXX)

include_directories(../include)
include_directories(../../publicShare/include)
include_directories(../../Log/include)

aux_source_directory(../src DIR_SRC)

ADD_LIBRARY(net SHARED ${DIR_SRC})

add_library(public SHARED IMPORTED)
set_target_properties(public PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}../bin/libpublic.so)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(public INTERFACE Threads::Threads)

set(CMAKE_BUILD_TYPE "Debug")
SET(LIBRARY_OUTPUT_PATH ../bin)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -fpermissive")