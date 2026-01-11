#!/bin/bash

echo "--- Nandadeep Server Installation (Linux) ---"

echo "[1/3] Installing Dependencies..."
sudo apt-get update
sudo apt-get install -y build-essential pkg-config \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgstrtspserver-1.0-dev gstreamer1.0-tools \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly

echo "[2/3] Creating Directories..."
mkdir -p recordings

echo "[3/3] Compiling Server..."
# Compiles all cpp files in the directory and links GStreamer
g++ -o video_server main.cpp StreamEngine.cpp VideoStorage.cpp SessionManager.cpp Logger.cpp \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0) \
    -lpthread -O2

echo "-------------------------------------------"
echo "Installation Complete."
echo "Run the server with: ./video_server"
