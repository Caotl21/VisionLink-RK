cmake_minimum_required(VERSION 3.10)

project(auv_vision_demo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(${OpenCV_INCLUDE_DIRS})
find_package(OpenCV REQUIRED)

include_directories("/usr/include/rga")
find_library(RGA_LIB rga)

include_directories("/usr/include/rockchip")
find_library(MPP_LIB rockchip_mpp)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include")
find_library(RKNN_LIB rknnrt PATHS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/lib NO_DEFAULT_PATH)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)
add_executable(auv_vision 
    src/main.cpp
    src/dma_utils.cpp
    src/mpp_encoder.cpp
    src/v4l2_utils.c
    src/yolo_detector.cpp
    src/postprocess.cc
)

target_link_libraries(auv_vision
    ${OpenCV_LIBS}
    ${RGA_LIB}
    ${MPP_LIB}
    ${RKNN_LIB}
    pthread
    dl
)