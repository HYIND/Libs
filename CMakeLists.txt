cmake_minimum_required(VERSION 3.16)

# 根项目
project(Libs)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8)
endif()

# set(CMAKE_BUILD_TYPE "Debug")

# 添加子目录
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/publicShare)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Net)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Simple) 

if(EchoServerSimple_ON)
    add_dependencies(EchoServerSimple public net)
endif()
if(EchoClientSimple_ON)
    add_dependencies(EchoClientSimple public net)
endif()
if(CoroutineSimple_ON)
    add_dependencies(CoroutineSimple public net)
endif()
if(ThreadPoolSimple_ON)
    add_dependencies(ThreadPoolSimple public net)
endif()
if(TimerSimple_ON)
    add_dependencies(TimerSimple public net)
endif()