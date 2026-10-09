cmake_minimum_required(VERSION 3.16)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../example.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../example.cmake)
endif()
# Phiên bản firmware — MỘT nguồn cho heartbeat và OTA (dạng X.Y.Z).
set(PROJECT_VER "0.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ie-hello-device)
