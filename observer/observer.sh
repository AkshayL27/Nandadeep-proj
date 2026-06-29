#!/bin/bash

# ==========================================
# DEFAULT CONFIGURATION (Change and forget)
# ==========================================
DEFAULT_NAME="TV_Room_1"
DEFAULT_SERVER_IP="10.222.18.93"
DEFAULT_VIDEO_DEVICE="/dev/video0"

NAME=$DEFAULT_NAME
SERVER_IP=$DEFAULT_SERVER_IP
VIDEO_DEV=$DEFAULT_VIDEO_DEVICE
FORCE_ENCODER=""
INSTALL_DEPS=false

# ==========================================
# PARSE RUNTIME FLAGS
# ==========================================
show_help() {
    echo "Usage: ./observer.sh [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -n, --name <name>       Set the Name of this camera node (Default: $DEFAULT_NAME)"
    echo "  -i, --ip <ip>           Set the IP of the server (Default: $DEFAULT_SERVER_IP)"
    echo "  -d, --device <path>     Set the video device path (Default: $DEFAULT_VIDEO_DEVICE)"
    echo "  -e, --encoder <type>    Force encoder type: 'hw' (v4l2) or 'sw' (x264) (Default: Auto-detect)"
    echo "  -I, --install           Install necessary dependencies (gstreamer, python3) before starting"
    echo "  -h, --help              Show this help message"
    echo ""
    exit 0
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        -n|--name) NAME="$2"; shift ;;
        -i|--ip) SERVER_IP="$2"; shift ;;
        -d|--device) VIDEO_DEV="$2"; shift ;;
        -e|--encoder) FORCE_ENCODER="$2"; shift ;;
        -I|--install) INSTALL_DEPS=true ;;
        -h|--help) show_help ;;
        *) echo "Unknown parameter passed: $1"; show_help ;;
    esac
    shift
done

# ==========================================
# INSTALL DEPENDENCIES (If Requested)
# ==========================================
if [ "$INSTALL_DEPS" = true ]; then
    echo "[Setup] Installing required dependencies with apt-get..."
    sudo apt-get update
    sudo apt-get install -y python3 gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly
    echo "[Setup] Dependencies installed successfully!"
    echo ""
fi

# ==========================================
# HARDWARE DETECTION & PIPELINE CONFIG
# ==========================================
HW_MODEL="Unknown"
if [ -f /sys/firmware/devicetree/base/model ]; then
    HW_MODEL=$(tr -d '\0' < /sys/firmware/devicetree/base/model)
fi

echo "[Init] Detected Hardware: $HW_MODEL"

# Determine optimal GStreamer encoder based on hardware
ENCODER_PIPELINE=""

if [ "$FORCE_ENCODER" == "hw" ]; then
    echo "[Config] Forcing Hardware Encoder (v4l2h264enc) via flag"
    ENCODER_PIPELINE="v4l2h264enc extra-controls=\"controls,h264_profile=4,video_bitrate=10000000;\" ! video/x-h264,profile=high"
elif [ "$FORCE_ENCODER" == "sw" ]; then
    echo "[Config] Forcing Software Encoder (x264enc) via flag"
    ENCODER_PIPELINE="x264enc bitrate=10000 tune=zerolatency speed-preset=ultrafast ! video/x-h264,profile=high"
elif [[ "$HW_MODEL" == *"Raspberry Pi 4"* ]] || [[ "$HW_MODEL" == *"Raspberry Pi 3"* ]]; then
    echo "[Config] Selecting Hardware Encoder for Raspberry Pi 3/4 (v4l2h264enc)"
    ENCODER_PIPELINE="v4l2h264enc extra-controls=\"controls,h264_profile=4,video_bitrate=10000000;\" ! video/x-h264,profile=high"
elif [[ "$HW_MODEL" == *"Raspberry Pi 5"* ]]; then
    echo "[Config] Selecting Software Encoder for Raspberry Pi 5 (x264enc)"
    ENCODER_PIPELINE="x264enc bitrate=10000 tune=zerolatency speed-preset=ultrafast ! video/x-h264,profile=high"
else
    echo "[Config] Hardware unknown or Generic. Defaulting to Software Encoder (x264enc)"
    ENCODER_PIPELINE="x264enc bitrate=10000 tune=zerolatency speed-preset=ultrafast ! video/x-h264,profile=high"
fi

# ==========================================
# DISCOVERY & ASSIGNMENT
# ==========================================
echo "[Discovery] Registering Camera '$NAME' with Server at $SERVER_IP..."

PORT=$(python3 -c "
import socket, sys
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(3.0)
    s.sendto(f'DISCOVER {sys.argv[1]}'.encode(), (sys.argv[2], 5001))
    data, _ = s.recvfrom(1024)
    resp = data.decode().strip().split()
    if len(resp) >= 2 and resp[0] == 'ASSIGN': 
        print(resp[1])
except Exception as e:
    pass
" "$NAME" "$SERVER_IP")

if [ -z "$PORT" ]; then
    echo "[Error] Server did not respond with an assigned port. Is it running?"
    exit 1
fi

echo "[Success] Assigned to stream on Port: $PORT"
echo "[Stream] Starting video stream from $VIDEO_DEV to $SERVER_IP:$PORT via MPEG-TS UDP..."

# Build and execute the full pipeline intelligently
PIPELINE="gst-launch-1.0 -v v4l2src device=$VIDEO_DEV ! image/jpeg,width=1920,height=1080,framerate=30/1 ! jpegdec ! videoconvert ! $ENCODER_PIPELINE ! h264parse config-interval=-1 ! mpegtsmux ! udpsink host=$SERVER_IP port=$PORT"

echo "[Exec] $PIPELINE"
eval $PIPELINE
