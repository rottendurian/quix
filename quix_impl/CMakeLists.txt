cmake_minimum_required(VERSION 3.24)

project(quix_impl)

add_library(${PROJECT_NAME} STATIC quix_vma.cpp quix_stb.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS} ${stb_SOURCE_DIR})
