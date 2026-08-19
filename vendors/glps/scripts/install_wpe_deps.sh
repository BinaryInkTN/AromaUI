#!/bin/bash
# Install WPE WebKit and FDO backend dependencies

if command -v apt-get &> /dev/null; then
    sudo apt-get update
    sudo apt-get install -y libwpe-1.0-1 libwpebackend-fdo-1.0-1 libwpewebkit-1.0-3 \
        libwpewebkit-1.0-dev libwpebackend-fdo-1.0-dev libwpe-1.0-dev \
        libegl1-mesa-dev libwayland-dev wayland-protocols \
        libxkbcommon-dev libglib2.0-dev libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev libsoup2.4-dev
elif command -v dnf &> /dev/null; then
    sudo dnf install -y wpewebkit-devel wpebackend-fdo-devel \
        libwpe-devel mesa-libEGL-devel wayland-devel \
        wayland-protocols-devel libxkbcommon-devel \
        glib2-devel gstreamer1-devel gstreamer1-plugins-base-devel \
        libsoup-devel
elif command -v pacman &> /dev/null; then
    sudo pacman -S --noconfirm wpewebkit wpebackend-fdo \
        libwpe wayland libxkbcommon glib2 \
        gstreamer gst-plugins-base libsoup
else
    echo "Unsupported package manager. Please install WPE WebKit and FDO backend manually."
    exit 1
fi

echo "WPE and FDO dependencies installed successfully!"
