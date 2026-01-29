#!/bin/bash
# FTorrent Linux Dependency Setup Script
# Installs necessary tools and libraries for both building and packaging.

set -e

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    OS=$(uname -s)
fi

echo "--- FTorrent Linux Setup ---"
echo "Detecting OS: $OS"

# Function to run commands with sudo if available
run_as_root() {
    if [ "$EUID" -ne 0 ]; then
        if command -v sudo >/dev/null 2>&1; then
            sudo "$@"
        else
            echo "Error: This script must be run as root or with sudo."
            exit 1
        fi
    else
        "$@"
    fi
}

case $OS in
    ubuntu|debian|raspbian|linuxmint|pop)
        echo "Installing dependencies for Debian-based system..."
        export DEBIAN_FRONTEND=noninteractive
        run_as_root apt-get update
        run_as_root apt-get install -y \
            build-essential \
            cmake \
            libfltk1.3-dev \
            libtorrent-rasterbar-dev \
            libpng-dev \
            libjpeg-dev \
            zlib1g-dev \
            pkg-config \
            rpm \
            git
        ;;
    fedora|rhel|centos|almalinux|rocky)
        echo "Installing dependencies for RedHat-based system..."
        run_as_root dnf install -y \
            gcc-c++ \
            cmake \
            fltk-devel \
            libtorrent-rasterbar-devel \
            libpng-devel \
            libjpeg-turbo-devel \
            zlib-devel \
            rpm-build \
            pkgconfig \
            git
        ;;
    arch|manjaro)
        echo "Installing dependencies for Arch Linux..."
        run_as_root pacman -S --needed --noconfirm \
            base-devel \
            cmake \
            fltk \
            libtorrent-rasterbar \
            libpng \
            libjpeg-turbo \
            zlib \
            rpm-tools \
            git
        ;;
    *)
        echo "Unsupported OS: $OS. Please install dependencies manually."
        echo "Required: C++20 compiler, CMake, FLTK, libtorrent-rasterbar, libpng, libjpeg, zlib."
        exit 1
        ;;
esac

echo "Setup completed successfully!"
