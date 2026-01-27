#!/bin/bash
# FTorrent Linux Dependency Setup Script
# Installs necessary tools and libraries for both building and packaging.

if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    OS=$(uname -s)
fi

echo "--- FTorrent Linux Setup ---"
echo "Detecting OS: $OS"

case $OS in
    ubuntu|debian|raspbian)
        echo "Installing dependencies for Debian-based system..."
        sudo apt update
        sudo apt install -y build-essential cmake libfltk1.3-dev libtorrent-rasterbar-dev \
                            libpng-dev libjpeg-dev zlib1g-dev pkg-config rpm
        ;;
    fedora)
        echo "Installing dependencies for Fedora..."
        sudo dnf install -y gcc-c++ cmake fltk-devel libtorrent-rasterbar-devel \
                            libpng-devel libjpeg-turbo-devel zlib-devel rpm-build
        ;;
    arch)
        echo "Installing dependencies for Arch Linux..."
        sudo pacman -S --needed base-devel cmake fltk libtorrent-rasterbar \
                                libpng libjpeg-turbo zlib rpm-tools
        ;;
    *)
        echo "Unsupported OS: $OS. Please install dependencies manually."
        ;;
esac
