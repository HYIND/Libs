cmake_minimum_required (VERSION 3.16)

project(publicShare C CXX)

include_directories(../include)

aux_source_directory(../src DIR_SRC)

ADD_LIBRARY(public SHARED ${DIR_SRC})


IF (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(public libmysqlclient.so)
    target_link_libraries(public librabbitmq.so)
    target_link_libraries(public libredis++.so)
ENDIF ()

# set(CMAKE_BUILD_TYPE "Debug")

SET(LIBRARY_OUTPUT_PATH ../bin)
IF (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread -lfmt -lrabbitmq")
ENDIF ()