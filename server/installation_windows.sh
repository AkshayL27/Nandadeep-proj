#!/bin/bash
# This script is intended for the MSYS2 MinGW 64-bit environment.
# 1. Install MSYS2 from https://www.msys2.org/
# 2. Open "MSYS2 MinGW 64-bit" terminal
# 3. Navigate to this folder and run: ./installation_windows.sh

echo "--- Nandadeep Server Installation (Windows MSYS2) ---"

if ! command -v pacman &> /dev/null; then
    echo "Error: 'pacman' not found. Please run this script inside MSYS2 MinGW 64-bit."
    exit 1
fi

echo "[1/3] Installing Dependencies via Pacman..."
pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-pkg-config \
    mingw-w64-x86_64-gstreamer mingw-w64-x86_64-gst-rtsp-server \
    mingw-w64-x86_64-gst-plugins-base mingw-w64-x86_64-gst-plugins-good \
    mingw-w64-x86_64-gst-plugins-bad mingw-w64-x86_64-gst-plugins-ugly

echo "[2/3] Creating Directories..."
mkdir -p recordings

echo "[3/3] Compiling Server..."
g++ -o video_server.exe main.cpp StreamEngine.cpp VideoStorage.cpp SessionManager.cpp Logger.cpp \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0) \
    -lws2_32 -static-libgcc -static-libstdc++ -O2

echo "-------------------------------------------"
echo "Installation Complete."
echo "Run the server with: ./video_server.exe"
