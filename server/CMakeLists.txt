cmake_minimum_required(VERSION 3.10)
project(HospitalVideoServer)

set(CMAKE_CXX_STANDARD 17)

# Find GStreamer Packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 IMPORTED_TARGET)
pkg_check_modules(GST_RTSP REQUIRED gstreamer-rtsp-server-1.0 IMPORTED_TARGET)

# Define Sources
set(SOURCES
    main.cpp
    SessionManager.cpp
    StreamEngine.cpp
    VideoStorage.cpp
)

add_executable(VideoServer ${SOURCES})

# Link Libraries
target_link_libraries(VideoServer PkgConfig::GST PkgConfig::GST_RTSP stdc++fs)

# Windows specific (for compilation on Windows later)
if(WIN32)
    target_link_libraries(VideoServer ws2_32)
endif()