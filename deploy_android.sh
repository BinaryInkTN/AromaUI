#!/bin/bash
set -e

# Check for Gradle
if ! command -v gradle &> /dev/null; then
    if [ -f "./android_project/gradlew" ]; then
        GRADLE_CMD="./android_project/gradlew"
    else
        echo "gradle could not be found."
        echo "Please install gradle (e.g., sudo apt install gradle) or open 'android_project' in Android Studio."
        exit 1
    fi
else
    GRADLE_CMD="gradle"
fi

echo "Building AromaUI Android App (APK only)..."
cd android_project

# Check for local.properties
if [ ! -f "local.properties" ]; then
    echo "WARNING: local.properties not found."
    echo "If the build fails, ensure you have set ANDROID_HOME environment variable"
    echo "or create local.properties with 'sdk.dir=/path/to/android/sdk'"
fi

# Run Gradle Build (Assemble APK)
$GRADLE_CMD assembleDebug

echo "Build complete!"
echo "APK location: android_project/app/build/outputs/apk/debug/app-debug.apk"
