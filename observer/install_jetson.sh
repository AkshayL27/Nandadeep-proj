#!/bin/bash

# ==================================================================================
# Nandadeep Hospital Video Server — Jetson Orin Nano Dependency Installer
#
# Installs all packages required to run observer_jetson.sh on a Jetson Orin Nano
# running NVIDIA JetPack 5.x or 6.x (Ubuntu 20.04 / 22.04 base).
#
# Must be run with sudo / as root:
#   sudo ./install_jetson.sh
#
# What this script installs:
#   • GStreamer core + all plugin packages (good, bad, ugly, libav)
#   • NVIDIA GStreamer plugins (nvv4l2*, nvargus*) — already on JetPack but
#     the script verifies and installs any that are missing
#   • python3 (for the UDP discovery handshake)
#   • v4l-utils  (optional helpers: v4l2-ctl, v4l2-compliance)
#   • argus-related packages for CSI camera support
# ==================================================================================

set -e  # Exit immediately on any error

# ── Colour helpers ──────────────────────────────────────────────────────────────
RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[1;33m'
BLU='\033[0;34m'
NC='\033[0m'  # No Colour

info()    { echo -e "${BLU}[Info]${NC}    $*"; }
success() { echo -e "${GRN}[OK]${NC}      $*"; }
warn()    { echo -e "${YLW}[Warning]${NC} $*"; }
error()   { echo -e "${RED}[Error]${NC}   $*"; }

# ── Root check ──────────────────────────────────────────────────────────────────
if [ "$EUID" -ne 0 ]; then
    error "This script must be run as root.  Use: sudo ./install_jetson.sh"
    exit 1
fi

echo ""
echo "  ╔══════════════════════════════════════════════════╗"
echo "  ║   Nandadeep — Jetson Orin Nano Installer         ║"
echo "  ╚══════════════════════════════════════════════════╝"
echo ""

# ── Detect JetPack version ──────────────────────────────────────────────────────
JETPACK_VERSION="unknown"
if [ -f /etc/nv_tegra_release ]; then
    JETPACK_VERSION=$(head -1 /etc/nv_tegra_release)
    info "JetPack release: $JETPACK_VERSION"
elif [ -f /etc/apt/sources.list.d/nvidia-l4t-apt-source.list ]; then
    info "Found NVIDIA L4T APT source — JetPack is present."
else
    warn "Cannot detect JetPack version. Are you running this on a Jetson?"
    warn "Continuing anyway — some NVIDIA packages may fail to install."
fi

# ── Hardware model ──────────────────────────────────────────────────────────────
HW_MODEL="Unknown"
if [ -f /sys/firmware/devicetree/base/model ]; then
    HW_MODEL=$(tr -d '\0' < /sys/firmware/devicetree/base/model)
fi
info "Hardware model: $HW_MODEL"
echo ""

# ── Step 1: Update package lists ────────────────────────────────────────────────
info "Updating APT package lists..."
apt-get update -qq
success "APT package lists updated."
echo ""

# ── Step 2: Core system utilities ───────────────────────────────────────────────
info "Installing core utilities (python3, curl, v4l-utils)..."
apt-get install -y --no-install-recommends \
    python3 \
    curl \
    v4l-utils \
    usbutils
success "Core utilities installed."
echo ""

# ── Step 3: GStreamer core and standard plugins ──────────────────────────────────
# These are the upstream (non-NVIDIA) GStreamer packages available from
# the standard Ubuntu/Debian repos.  They are needed even on Jetson because
# NVIDIA's plugins build on top of the GStreamer core.
info "Installing GStreamer core and standard plugins..."
apt-get install -y --no-install-recommends \
    libgstreamer1.0-0 \
    libgstreamer1.0-dev \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-python3-plugin-loader \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev
success "GStreamer standard plugins installed."
echo ""

# ── Step 4: NVIDIA GStreamer / Multimedia API packages ──────────────────────────
# On JetPack 5.x these are named  nvidia-l4t-multimedia  (L4T R35)
# On JetPack 6.x they may be named  nvidia-jetpack-multimedia  (L4T R36)
# We try both and skip gracefully if a package is not found in this repo.

install_optional() {
    local pkg="$1"
    if apt-cache show "$pkg" &>/dev/null 2>&1; then
        apt-get install -y --no-install-recommends "$pkg" && \
            success "Installed: $pkg" || warn "Failed to install $pkg (non-fatal)"
    else
        warn "Package not found in APT cache: $pkg (may not be needed for this JetPack version)"
    fi
}

info "Installing NVIDIA L4T / Jetson Multimedia packages..."

# Core multimedia framework — provides nvv4l2*, nvvidconv, nvjpegdec, etc.
install_optional "nvidia-l4t-multimedia"
install_optional "nvidia-l4t-multimedia-utils"

# GStreamer NVIDIA plugins  (nvv4l2h264enc, nvarguscamerasrc, nvivafilter…)
install_optional "nvidia-l4t-gstreamer"

# Argus (ISP daemon) — required for CSI cameras via nvarguscamerasrc
install_optional "nvidia-l4t-camera"
install_optional "libargus-dev"
install_optional "libargus0"

# JetPack 6.x alternative names
install_optional "nvidia-jetpack-multimedia"
install_optional "nvidia-jetpack-gstreamer"

echo ""

# ── Step 5: x264 software encoder (fallback) ────────────────────────────────────
info "Installing x264 software encoder (fallback)..."
apt-get install -y --no-install-recommends \
    gstreamer1.0-plugins-ugly \
    libx264-dev
success "x264 encoder installed."
echo ""

# ── Step 6: Verify critical GStreamer plugins ────────────────────────────────────
echo ""
info "Verifying GStreamer plugin availability..."
echo ""

check_plugin() {
    local plugin="$1"
    if gst-inspect-1.0 "$plugin" &>/dev/null 2>&1; then
        success "✓ $plugin"
    else
        warn "✗ $plugin  — NOT FOUND (see notes below)"
    fi
}

check_plugin "v4l2src"
check_plugin "nvv4l2h264enc"
check_plugin "nvvidconv"
check_plugin "nvarguscamerasrc"
check_plugin "x264enc"
check_plugin "jpegdec"
check_plugin "h264parse"
check_plugin "mpegtsmux"
check_plugin "udpsink"

echo ""

# ── Step 7: USB camera quick-check ──────────────────────────────────────────────
info "Checking for connected video devices..."
if ls /dev/video* &>/dev/null 2>&1; then
    for dev in /dev/video*; do
        desc=$(v4l2-ctl --device="$dev" --info 2>/dev/null | grep "Card type" | sed 's/.*: //' || echo "unknown")
        success "Found: $dev  ($desc)"
    done
else
    warn "No /dev/video* devices found. USB cameras may not be connected."
fi
echo ""

# ── Done ─────────────────────────────────────────────────────────────────────────
echo "  ─────────────────────────────────────────────────────"
success "Installation complete!"
echo ""
echo "  Next steps:"
echo "    1. Make the observer executable:"
echo "         chmod +x observer_jetson.sh"
echo ""
echo "    2. Run with USB camera (default):"
echo "         ./observer_jetson.sh -n 'Ward_Cam_1' -i <SERVER_IP>"
echo ""
echo "    3. Run with CSI camera:"
echo "         ./observer_jetson.sh -s csi -n 'Ward_Cam_1' -i <SERVER_IP>"
echo ""
echo "    4. Force software encoder if NVENC is unavailable:"
echo "         ./observer_jetson.sh -e sw -n 'Ward_Cam_1' -i <SERVER_IP>"
echo ""
echo "  If nvv4l2h264enc was NOT found above, reboot the Jetson and"
echo "  re-run this installer — the NVIDIA kernel modules need to be"
echo "  loaded before GStreamer can see the NVENC plugins."
echo "  ─────────────────────────────────────────────────────"
echo ""
