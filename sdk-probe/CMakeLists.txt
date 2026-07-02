# Linux-correct CMake build for obsbot-sdk-probe (OPTIONAL).
#
# The Makefile is the primary build for this spike. This file is provided as an
# alternative for anyone who prefers CMake. It deliberately does NOT copy the
# upstream OBSBOT_Sample/CMakeLists.txt logic: that file's non-Apple branch
# points at Windows paths (../windows/win64-*) and links `libdev` the MSVC way,
# which is wrong on Linux. Here we add a proper Linux/x86_64 branch.

cmake_minimum_required(VERSION 3.16)
project(obsbot_sdk_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Root of the extracted SDK, relative to this directory. Override with
# -DSDK_ROOT=/abs/path if the SDK lives elsewhere.
set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/libdev_v2.1.0_8" CACHE PATH
    "Path to extracted libdev SDK root")

set(SDK_INCLUDE_PATH "${SDK_ROOT}/include")

if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(SDK_LINK_PATH "${SDK_ROOT}/macos/arm64-release")
    else()
        set(SDK_LINK_PATH "${SDK_ROOT}/macos/x86_64-release")
    endif()
elseif(UNIX)
    # Linux. Pick arch dir from the target processor.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(SDK_LINK_PATH "${SDK_ROOT}/linux/arm64-release")
    else()
        set(SDK_LINK_PATH "${SDK_ROOT}/linux/x86_64-release")
    endif()
else()
    message(FATAL_ERROR "This probe CMake supports Linux and macOS only.")
endif()

add_executable(obsbot-sdk-probe main.cpp)

target_include_directories(obsbot-sdk-probe PRIVATE "${SDK_INCLUDE_PATH}")
target_link_directories(obsbot-sdk-probe PRIVATE "${SDK_LINK_PATH}")
target_link_libraries(obsbot-sdk-probe PRIVATE dev pthread)

# Embed an rpath so libdev.so.1.0.3 is found next to the SDK tree at runtime,
# with no global install. $ORIGIN is the executable's own directory.
set_target_properties(obsbot-sdk-probe PROPERTIES
    BUILD_RPATH "${SDK_LINK_PATH}"
    INSTALL_RPATH "${SDK_LINK_PATH}")
