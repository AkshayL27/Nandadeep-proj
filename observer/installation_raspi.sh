#!/bin/bash

echo "--- Nandadeep Observer Node Installation (Raspberry Pi) ---"

echo "[1/2] Installing Dependencies..."
sudo apt-get update
# Install GStreamer tools, plugins (including libcamera support), and netcat
sudo apt-get install -y gstreamer1.0-tools gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libcamera netcat-openbsd

echo "[2/2] Setting Permissions..."
chmod +x connection.sh
chmod +x discovery.sh

echo "-------------------------------------------"
echo "Installation Complete."
echo "1. Edit 'discovery.sh' and replace <SERVER_IP> with your server's address."
echo "2. Run ./discovery.sh to register this camera."
echo "3. Run ./connection.sh to start streaming."
