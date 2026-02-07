#!/bin/bash
set -e

SDK_ROOT="$HOME/Android/Sdk"
CMDLINE_TOOLS_URL="https://dl.google.com/android/repository/commandlinetools-linux-10406996_latest.zip"
TMP_ZIP="cmdline-tools.zip"

echo "Setting up Android SDK at $SDK_ROOT..."

mkdir -p "$SDK_ROOT/cmdline-tools"

if [ ! -d "$SDK_ROOT/cmdline-tools/latest" ]; then
    echo "Downloading Command Line Tools..."
    wget -O $TMP_ZIP $CMDLINE_TOOLS_URL
    
    if command -v unzip &> /dev/null; then
        unzip -q $TMP_ZIP -d "$SDK_ROOT/cmdline-tools"
    elif command -v python3 &> /dev/null; then
        echo "unzip not found, using python3..."
        python3 -c "import zipfile; import sys; zipfile.ZipFile(sys.argv[1], 'r').extractall(sys.argv[2])" "$TMP_ZIP" "$SDK_ROOT/cmdline-tools"
    else
        echo "Error: Neither 'unzip' nor 'python3' found. Please install unzip."
        exit 1
    fi

    if [ -d "$SDK_ROOT/cmdline-tools/cmdline-tools" ]; then
        mv "$SDK_ROOT/cmdline-tools/cmdline-tools" "$SDK_ROOT/cmdline-tools/latest"
    elif [ -d "$SDK_ROOT/cmdline-tools/bin" ]; then
        mkdir -p "$SDK_ROOT/cmdline-tools/latest"
        mv "$SDK_ROOT/cmdline-tools/bin" "$SDK_ROOT/cmdline-tools/latest/"
        mv "$SDK_ROOT/cmdline-tools/lib" "$SDK_ROOT/cmdline-tools/latest/"
        mv "$SDK_ROOT/cmdline-tools/source.properties" "$SDK_ROOT/cmdline-tools/latest/" 2>/dev/null || true
    fi

    rm $TMP_ZIP
else
    echo "Command Line Tools already installed."
fi

if [ -f "$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" ]; then
    SDKMANAGER="$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
else
    SDKMANAGER=$(find "$SDK_ROOT" -name sdkmanager -type f | head -n 1)
fi

if [ -z "$SDKMANAGER" ]; then
    echo "Error: sdkmanager not found. Resetting setup..."
    rm -rf "$SDK_ROOT/cmdline-tools"
    echo "Please run this script again."
    exit 1
fi

echo "Accepting Licenses..."
yes | $SDKMANAGER --licenses > /dev/null 2>&1

echo "Installing Platform Tools and SDK 34..."
$SDKMANAGER "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;25.1.8937393" "cmake;3.22.1"

echo "Android SDK Setup Complete!"