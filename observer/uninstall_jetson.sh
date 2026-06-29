#!/bin/bash

# ==================================================================================
# Nandadeep Hospital Video Server — Jetson Orin Nano Uninstaller
#
# Reverses the changes made by install_jetson.sh, restoring the board to its
# original JetPack configuration as received.
#
# ⚠️  IMPORTANT SAFETY NOTES:
#   • NVIDIA L4T packages (nvidia-l4t-*) ship PRE-INSTALLED with JetPack.
#     This script will NOT remove them by default because doing so can render
#     the board unbootable.  Use --remove-nvidia only if you intend to do a
#     full reflash afterwards.
#   • This script only removes packages that install_jetson.sh added on top
#     of the base JetPack image.
#
# Usage:
#   sudo ./uninstall_jetson.sh              # Safe removal (keeps NVIDIA L4T packages)
#   sudo ./uninstall_jetson.sh --remove-nvidia  # ⚠️  Also removes NVIDIA packages
#   sudo ./uninstall_jetson.sh --dry-run    # Show what would be removed, do nothing
# ==================================================================================

set -e

# ── Colour helpers ──────────────────────────────────────────────────────────────
RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[1;33m'
BLU='\033[0;34m'
MAG='\033[0;35m'
NC='\033[0m'

info()    { echo -e "${BLU}[Info]${NC}    $*"; }
success() { echo -e "${GRN}[OK]${NC}      $*"; }
warn()    { echo -e "${YLW}[Warning]${NC} $*"; }
error()   { echo -e "${RED}[Error]${NC}   $*"; }
danger()  { echo -e "${RED}[DANGER]${NC}  $*"; }

# ── Parse flags ─────────────────────────────────────────────────────────────────
REMOVE_NVIDIA=false
DRY_RUN=false

for arg in "$@"; do
    case "$arg" in
        --remove-nvidia) REMOVE_NVIDIA=true ;;
        --dry-run)       DRY_RUN=true ;;
        --help|-h)
            echo ""
            echo "  Usage: sudo ./uninstall_jetson.sh [OPTIONS]"
            echo ""
            echo "  Options:"
            echo "    (none)            Safe uninstall — removes only non-JetPack packages"
            echo "    --remove-nvidia   Also remove NVIDIA L4T packages (⚠ may break boot)"
            echo "    --dry-run         Show what would be done without actually doing it"
            echo "    --help            Show this message"
            echo ""
            exit 0
            ;;
        *) error "Unknown argument: $arg"; exit 1 ;;
    esac
done

# ── Root check ──────────────────────────────────────────────────────────────────
if [ "$EUID" -ne 0 ]; then
    error "This script must be run as root.  Use: sudo ./uninstall_jetson.sh"
    exit 1
fi

# ── Dry-run wrapper ─────────────────────────────────────────────────────────────
apt_remove() {
    # Removes a package only if it is currently installed, skips otherwise.
    local pkg="$1"
    if dpkg -l "$pkg" 2>/dev/null | grep -q "^ii"; then
        if [ "$DRY_RUN" = true ]; then
            warn "[DRY-RUN] Would remove: $pkg"
        else
            apt-get remove -y --purge "$pkg" 2>/dev/null && \
                success "Removed: $pkg" || \
                warn "Could not remove $pkg (non-fatal)"
        fi
    else
        info "Not installed, skipping: $pkg"
    fi
}

# ── Header ──────────────────────────────────────────────────────────────────────
echo ""
echo "  ╔══════════════════════════════════════════════════╗"
echo "  ║   Nandadeep — Jetson Orin Nano Uninstaller       ║"
echo "  ╚══════════════════════════════════════════════════╝"
echo ""

if [ "$DRY_RUN" = true ]; then
    warn "DRY-RUN MODE — no changes will be made."
    echo ""
fi

if [ "$REMOVE_NVIDIA" = true ]; then
    danger "──────────────────────────────────────────────────────────"
    danger " --remove-nvidia is set."
    danger " This will remove NVIDIA L4T / GStreamer packages that"
    danger " are part of the JetPack base image.  The board may fail"
    danger " to boot or lose display output after this step."
    danger " Only proceed if you plan to reflash the board."
    danger "──────────────────────────────────────────────────────────"
    echo ""
    if [ "$DRY_RUN" = false ]; then
        read -r -p "  Type 'yes' to confirm: " CONFIRM
        if [ "$CONFIRM" != "yes" ]; then
            info "Aborted."
            exit 0
        fi
    fi
    echo ""
fi

# ==========================================
# STEP 1 — Remove packages added by install_jetson.sh
# ==========================================
info "Step 1: Removing GStreamer standard plugin packages..."
echo ""

# These are the upstream (non-NVIDIA) packages install_jetson.sh added.
# gstreamer1.0-tools and gstreamer1.0-plugins-base are commonly pre-installed
# on JetPack too, so we remove them here but autoremove will clean orphans.
GSTREAMER_PACKAGES=(
    "gstreamer1.0-libav"
    "gstreamer1.0-plugins-bad"
    "gstreamer1.0-plugins-good"
    "gstreamer1.0-plugins-ugly"
    "gstreamer1.0-python3-plugin-loader"
    "libgstreamer-plugins-bad1.0-dev"
    "libgstreamer-plugins-base1.0-dev"
    "libgstreamer1.0-dev"
    "libx264-dev"
)

for pkg in "${GSTREAMER_PACKAGES[@]}"; do
    apt_remove "$pkg"
done
echo ""

# ==========================================
# STEP 2 — Remove utility packages added by the installer
# ==========================================
info "Step 2: Removing utility packages (v4l-utils, usbutils)..."
echo ""

# NOTE: python3 and curl are almost certainly present on the base JetPack
# image already (apt, pip, and many system tools depend on them), so we
# leave them in place to avoid breaking system functionality.
UTILITY_PACKAGES=(
    "v4l-utils"
    "usbutils"
)

for pkg in "${UTILITY_PACKAGES[@]}"; do
    apt_remove "$pkg"
done
echo ""

# ==========================================
# STEP 3 — Optionally remove NVIDIA L4T packages
# ==========================================
if [ "$REMOVE_NVIDIA" = true ]; then
    info "Step 3: Removing NVIDIA L4T / JetPack multimedia packages..."
    warn "This may affect GStreamer, display, and camera functionality."
    echo ""

    NVIDIA_PACKAGES=(
        "nvidia-l4t-gstreamer"
        "nvidia-l4t-camera"
        "nvidia-l4t-multimedia-utils"
        "nvidia-l4t-multimedia"
        "libargus-dev"
        "libargus0"
        "nvidia-jetpack-multimedia"
        "nvidia-jetpack-gstreamer"
    )

    for pkg in "${NVIDIA_PACKAGES[@]}"; do
        apt_remove "$pkg"
    done
    echo ""
else
    info "Step 3: Skipping NVIDIA L4T packages (JetPack base — kept for safety)."
    info "        Use --remove-nvidia to also remove these."
    echo ""
fi

# ==========================================
# STEP 4 — Clean up orphaned dependencies
# ==========================================
info "Step 4: Running autoremove to clean up orphaned dependencies..."
if [ "$DRY_RUN" = true ]; then
    warn "[DRY-RUN] Would run: apt-get autoremove -y"
else
    apt-get autoremove -y
    success "Autoremove complete."
fi
echo ""

# ==========================================
# STEP 5 — Clean APT cache
# ==========================================
info "Step 5: Clearing APT package cache..."
if [ "$DRY_RUN" = true ]; then
    warn "[DRY-RUN] Would run: apt-get clean"
else
    apt-get clean
    success "APT cache cleared."
fi
echo ""

# ==========================================
# STEP 6 — Verify current GStreamer state
# ==========================================
info "Step 6: Verifying post-uninstall GStreamer plugin state..."
echo ""

check_plugin() {
    local plugin="$1"
    local label="$2"
    if gst-inspect-1.0 "$plugin" &>/dev/null 2>&1; then
        warn "  Still present : $plugin  ($label)"
    else
        success "  Removed       : $plugin  ($label)"
    fi
}

if command -v gst-inspect-1.0 &>/dev/null; then
    check_plugin "nvv4l2h264enc"     "Jetson NVENC encoder"
    check_plugin "nvvidconv"         "Jetson colour converter"
    check_plugin "nvarguscamerasrc"  "CSI camera source (Argus)"
    check_plugin "x264enc"           "Software H.264 encoder"
    check_plugin "gstreamer1.0-plugins-bad" "bad plugins meta-package"
else
    warn "gst-inspect-1.0 not found — GStreamer may have been fully removed."
fi
echo ""

# ── Done ─────────────────────────────────────────────────────────────────────────
echo "  ─────────────────────────────────────────────────────"
success "Uninstall complete!"
echo ""
if [ "$REMOVE_NVIDIA" = false ]; then
    echo "  The board's NVIDIA L4T / JetPack packages are intact."
    echo "  The system should be in the same state as when received,"
    echo "  minus any upstream GStreamer packages added by install_jetson.sh."
else
    danger "  NVIDIA L4T packages were removed."
    danger "  A full JetPack reflash is recommended before further use."
    echo "  Reflash guide: https://developer.nvidia.com/embedded/learn/get-started-jetson-orin-nano-devkit"
fi
echo "  ─────────────────────────────────────────────────────"
echo ""
