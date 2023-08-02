cmake_minimum_required(VERSION 3.10)

# 设定工程名和版本号
project(net)

set(MAIN_PATH ${PROJECT_SOURCE_DIR}/..)

# 默认存放静态库的文件夹位置
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${MAIN_PATH}/lib/linux)

# 默认存放动态库的文件夹位置
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${MAIN_PATH}/bin/linux)

# 存放可执行软件的目录
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${MAIN_PATH}/bin/linux)

# 默认存放库文件的位置，如果产生的是静态库并且没有指定 CMAKE_ARCHIVE_OUTPUT_DIRECTORY 则存放在该目录下，动态库也类似
# set(LIBRARY_OUTPUT_PATH ${MAIN_PATH}/)

# 源文件路径
set(SRC_PATH ${MAIN_PATH}/src)

# include 路径
set(INCLUDE_PATH ${MAIN_PATH}/include)

# depends 路径
set(DEPENDS_PATH ${MAIN_PATH}/../depends)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# include
include_directories(${INCLUDE_PATH})
include_directories(${SRC_PATH})
include_directories(${DEPENDS_PATH}/platform/base/1.0.1/include)

# 源文件
aux_source_directory(${SRC_PATH} SRC_FILE)
aux_source_directory(${SRC_PATH}/poller SRC_FILE)

# 其他库
LINK_DIRECTORIES(${DEPENDS_PATH}/platform/base/1.0.1/lib/linux)

# 生成静态库
add_library(net ${SRC_FILE})
target_link_libraries(net base)
