cmake_minimum_required(VERSION 3.14)
project(HospitalVideoServer)

set(CMAKE_CXX_STANDARD 17)

# Find GStreamer Packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 IMPORTED_TARGET)
pkg_check_modules(GST_RTSP REQUIRED gstreamer-rtsp-server-1.0 IMPORTED_TARGET)

# Define Core Sources (excluding main)
set(CORE_SOURCES
    SessionManager.cpp
    StreamEngine.cpp
    VideoStorage.cpp
    alert.cpp
)

# Create Core Library
add_library(VideoServerCore STATIC ${CORE_SOURCES})
target_link_libraries(VideoServerCore PUBLIC PkgConfig::GST PkgConfig::GST_RTSP stdc++fs)
if(WIN32)
    target_link_libraries(VideoServerCore PUBLIC ws2_32)
endif()

# Main Executable
add_executable(VideoServer main.cpp)
target_link_libraries(VideoServer PRIVATE VideoServerCore)

# --- Testing Setup ---
enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(ServerTests
    tests/SessionManagerTest.cpp
    tests/VideoStorageTest.cpp
)

target_link_libraries(ServerTests PRIVATE VideoServerCore gtest_main)

include(GoogleTest)
gtest_discover_tests(ServerTests)