cmake_minimum_required(VERSION 3.16)

# 根项目
project(Libs)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加子目录
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/publicShare)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Net)
# add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Simple)