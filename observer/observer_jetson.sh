#!/bin/bash

# ==================================================================================
# Nandadeep Hospital Video Server — Observer Script
# Target Hardware: NVIDIA Jetson Orin Nano Developer Kit (JetPack 5.x / 6.x)
#
# This script:
#  1. Detects whether it is running on a Jetson device
#  2. Auto-selects the optimal GStreamer pipeline for the Jetson's NVENC hardware
#  3. Supports both CSI cameras (nvarguscamerasrc) and USB cameras (v4l2src)
#  4. Registers with the video server and streams H.264 over MPEG-TS UDP
#
# Usage:
#   ./observer_jetson.sh [OPTIONS]
#
# Run ./observer_jetson.sh --help for full option list.
# Run ./install_jetson.sh first to install all required dependencies.
# ==================================================================================

# ==========================================
# DEFAULT CONFIGURATION (Change and forget)
# ==========================================
DEFAULT_NAME="Jetson_Cam_1"
DEFAULT_SERVER_IP="10.222.18.93"
DEFAULT_VIDEO_DEVICE="/dev/video0"
DEFAULT_SOURCE="usb"       # 'usb' for v4l2src, 'csi' for nvarguscamerasrc

NAME=$DEFAULT_NAME
SERVER_IP=$DEFAULT_SERVER_IP
VIDEO_DEV=$DEFAULT_VIDEO_DEVICE
SOURCE_TYPE=$DEFAULT_SOURCE
FORCE_ENCODER=""           # 'hw' = nvv4l2h264enc, 'sw' = x264enc
WIDTH=1920
HEIGHT=1080
FRAMERATE=30
BITRATE=10000000           # bits per second (10 Mbps default for Jetson NVENC)

# ==========================================
# PARSE RUNTIME FLAGS
# ==========================================
show_help() {
    echo ""
    echo "  Nandadeep Observer — NVIDIA Jetson Orin Nano"
    echo "  ─────────────────────────────────────────────"
    echo "  Usage: ./observer_jetson.sh [OPTIONS]"
    echo ""
    echo "  Options:"
    echo "    -n, --name    <name>    Camera node name reported to the server  (Default: $DEFAULT_NAME)"
    echo "    -i, --ip      <ip>      Server IP address                        (Default: $DEFAULT_SERVER_IP)"
    echo "    -d, --device  <path>    Video device path (USB cameras)          (Default: $DEFAULT_VIDEO_DEVICE)"
    echo "    -s, --source  <type>    Camera source type: 'usb' or 'csi'       (Default: $DEFAULT_SOURCE)"
    echo "    -e, --encoder <type>    Force encoder: 'hw' (NVENC) or 'sw' (x264enc)"
    echo "    -W, --width   <px>      Stream width in pixels                   (Default: $WIDTH)"
    echo "    -H, --height  <px>      Stream height in pixels                  (Default: $HEIGHT)"
    echo "    -r, --rate    <fps>     Frames per second                        (Default: $FRAMERATE)"
    echo "    -b, --bitrate <bps>     Encoder bitrate in bits/s                (Default: $BITRATE)"
    echo "    -h, --help              Show this help message"
    echo ""
    echo "  Examples:"
    echo "    ./observer_jetson.sh -n 'Ward_A_Cam' -i 192.168.1.50"
    echo "    ./observer_jetson.sh -s csi -e hw"
    echo "    ./observer_jetson.sh -d /dev/video1 -e sw"
    echo ""
    exit 0
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        -n|--name)     NAME="$2";        shift ;;
        -i|--ip)       SERVER_IP="$2";   shift ;;
        -d|--device)   VIDEO_DEV="$2";   shift ;;
        -s|--source)   SOURCE_TYPE="$2"; shift ;;
        -e|--encoder)  FORCE_ENCODER="$2"; shift ;;
        -W|--width)    WIDTH="$2";       shift ;;
        -H|--height)   HEIGHT="$2";      shift ;;
        -r|--rate)     FRAMERATE="$2";   shift ;;
        -b|--bitrate)  BITRATE="$2";     shift ;;
        -h|--help)     show_help ;;
        *) echo "[Error] Unknown parameter: $1"; show_help ;;
    esac
    shift
done

# ==========================================
# HARDWARE DETECTION
# ==========================================
HW_MODEL="Unknown"
if [ -f /sys/firmware/devicetree/base/model ]; then
    HW_MODEL=$(tr -d '\0' < /sys/firmware/devicetree/base/model)
fi

echo ""
echo "  ╔══════════════════════════════════════════════╗"
echo "  ║   Nandadeep Observer — Jetson Orin Nano      ║"
echo "  ╚══════════════════════════════════════════════╝"
echo ""
echo "[Init] Detected Hardware : $HW_MODEL"
echo "[Init] Camera Name       : $NAME"
echo "[Init] Server IP         : $SERVER_IP"
echo "[Init] Source Type       : $SOURCE_TYPE"
echo "[Init] Video Device      : $VIDEO_DEV"
echo "[Init] Resolution        : ${WIDTH}x${HEIGHT} @ ${FRAMERATE}fps"
echo "[Init] Bitrate           : ${BITRATE} bps"
echo ""

# Verify this is actually a Jetson device
IS_JETSON=false
if [[ "$HW_MODEL" == *"Jetson"* ]] || [[ "$HW_MODEL" == *"NVIDIA"* ]]; then
    IS_JETSON=true
    echo "[Init] ✓ Jetson hardware confirmed."
else
    echo "[Warning] This device does not appear to be a Jetson (model: '$HW_MODEL')."
    echo "[Warning] Hardware encoder (nvv4l2h264enc) will likely fail. Consider using --encoder sw."
fi
echo ""

# ==========================================
# ENCODER PIPELINE SELECTION
# ==========================================
# Jetson NVENC encoder: nvv4l2h264enc (preferred — zero-copy via DMA)
# Fallback:            x264enc         (software, works on any Linux)
# ──────────────────────────────────────────
# Profile 4 = High, which gives best compression at a given bitrate.
# insert-sps-pps=1 forces SPS/PPS on every keyframe (required for MPEG-TS).

HW_ENCODER_PIPELINE="nvv4l2h264enc bitrate=${BITRATE} profile=High insert-sps-pps=1 iframeinterval=30 ! video/x-h264,profile=high,stream-format=byte-stream"
SW_ENCODER_PIPELINE="x264enc bitrate=$((BITRATE/1000)) tune=zerolatency speed-preset=ultrafast ! video/x-h264,profile=high"

ENCODER_PIPELINE=""

if [ "$FORCE_ENCODER" == "hw" ]; then
    echo "[Config] Forcing Hardware Encoder (nvv4l2h264enc / Jetson NVENC)"
    ENCODER_PIPELINE="$HW_ENCODER_PIPELINE"
elif [ "$FORCE_ENCODER" == "sw" ]; then
    echo "[Config] Forcing Software Encoder (x264enc)"
    ENCODER_PIPELINE="$SW_ENCODER_PIPELINE"
elif [ "$IS_JETSON" = true ]; then
    echo "[Config] Auto-selecting Hardware Encoder (nvv4l2h264enc) for Jetson"
    ENCODER_PIPELINE="$HW_ENCODER_PIPELINE"
else
    echo "[Config] Non-Jetson hardware — falling back to Software Encoder (x264enc)"
    ENCODER_PIPELINE="$SW_ENCODER_PIPELINE"
fi

# ==========================================
# CAMERA SOURCE PIPELINE SELECTION
# ==========================================
# CSI cameras (Raspberry Pi Camera, IMX219, IMX477, etc.) connected via the
# CSI/MIPI port use nvarguscamerasrc, which talks directly to the ISP.
#
# USB cameras use v4l2src. Jetson usually delivers MJPEG from USB webcams,
# so we decode it first (jpegdec) then colour-convert before encoding.

SOURCE_PIPELINE=""

if [ "$SOURCE_TYPE" == "csi" ]; then
    echo "[Config] Source: CSI camera via nvarguscamerasrc"
    # nvarguscamerasrc delivers NV12 natively — pipe directly into nvvidconv
    # nvvidconv converts to I420 which nvv4l2h264enc / videoconvert both accept
    SOURCE_PIPELINE="nvarguscamerasrc ! video/x-raw(memory:NVMM),width=${WIDTH},height=${HEIGHT},framerate=${FRAMERATE}/1,format=NV12 ! nvvidconv ! video/x-raw,format=I420"
else
    echo "[Config] Source: USB camera via v4l2src ($VIDEO_DEV)"
    # Most USB webcams expose MJPEG — decode it and convert colour space
    SOURCE_PIPELINE="v4l2src device=${VIDEO_DEV} ! image/jpeg,width=${WIDTH},height=${HEIGHT},framerate=${FRAMERATE}/1 ! jpegdec ! videoconvert ! video/x-raw,format=I420"
fi

echo ""

# ==========================================
# DISCOVERY & PORT ASSIGNMENT
# ==========================================
echo "[Discovery] Registering '$NAME' with server at $SERVER_IP:5001 ..."

PORT=$(python3 - "$NAME" "$SERVER_IP" <<'PYEOF'
import socket, sys

name      = sys.argv[1]
server_ip = sys.argv[2]

try:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(5.0)
    s.sendto(f'DISCOVER {name}'.encode(), (server_ip, 5001))
    data, _ = s.recvfrom(1024)
    resp = data.decode().strip().split()
    if len(resp) >= 2 and resp[0] == 'ASSIGN':
        print(resp[1])
except Exception as e:
    sys.stderr.write(f"[Discovery Error] {e}\n")
PYEOF
)

if [ -z "$PORT" ]; then
    echo ""
    echo "[Error] Server did not respond with an assigned port."
    echo "        → Is the Nandadeep server running at $SERVER_IP ?"
    echo "        → Check firewall rules for UDP port 5001."
    echo ""
    exit 1
fi

echo "[Discovery] ✓ Assigned streaming port: $PORT"
echo ""

# ==========================================
# BUILD & LAUNCH GSTREAMER PIPELINE
# ==========================================
# Full pipeline topology:
#
#   [Camera Source] → [Colour Convert] → [HW/SW Encoder] → [H264 Parse]
#       → [MPEG-TS Mux] → [UDP Sink → Server:PORT]
#
# h264parse config-interval=-1 : re-sends SPS/PPS before every IDR frame,
#   ensuring the server can recover from any dropped packets.

PIPELINE="gst-launch-1.0 -v \
    ${SOURCE_PIPELINE} ! \
    ${ENCODER_PIPELINE} ! \
    h264parse config-interval=-1 ! \
    mpegtsmux ! \
    udpsink host=${SERVER_IP} port=${PORT} sync=false"

echo "[Stream] Starting stream → udp://${SERVER_IP}:${PORT}"
echo "[Exec]   $PIPELINE"
echo ""

# Trap SIGINT/SIGTERM for clean exit
trap 'echo ""; echo "[Stream] Stopped."; exit 0' SIGINT SIGTERM

eval $PIPELINE

EXIT_CODE=$?
if [ $EXIT_CODE -ne 0 ]; then
    echo ""
    echo "[Error] GStreamer pipeline exited with code $EXIT_CODE."
    echo "        Common causes:"
    echo "          • NVENC plugin not found → run ./install_jetson.sh"
    echo "          • Camera not accessible  → check 'ls ${VIDEO_DEV}' or CSI connection"
    echo "          • Unsupported resolution  → try a lower --width / --height"
    echo "          • Wrong source type      → try --source usb or --source csi"
    echo ""
    exit $EXIT_CODE
fi
