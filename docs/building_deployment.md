<b> Author: AHMED ALI Mohamed Yassine </b>

<br/>

This document describes the complete deployment workflow for AromaUI projects using the `aroma` CLI. It focuses strictly on building, signing, packaging, and distributing applications for Linux and Android.

## 1. Build Targets Overview

AromaUI supports deployment to:

* Linux (native executable)
* Android Debug APK (testing)
* Android Release APK (signed, production)
* Android App Bundle (AAB, Play Store)

All deployment commands must be executed from the root of your Aroma project.


# Linux Deployment

## Build Linux Binary

```
aroma build linux
```

This command:

* Creates a `build/` directory
* Runs CMake configuration
* Compiles using Make

Output executable:

```
build/<project_name>
```

## Run Linux Build

```
aroma run linux
```

This builds (if needed) and launches the executable.

For distribution, package the binary along with any required shared libraries.


# Android Deployment

Android deployment supports debug and release builds.

## Debug Build (Testing)

### Build Debug APK

```
aroma build android
```

Output:

```
android/app/build/outputs/apk/debug/app-debug.apk
```

### Install on Connected Device

```
aroma run android
```

Requirements:

* USB debugging enabled
* `adb devices` shows a connected device

### Run in Emulator

```
aroma run android --emu
```

This will:

* Create an AVD if needed
* Start the emulator
* Wait for boot completion
* Install debug APK
* Launch the application


# Release Deployment (Production)

Release builds require a signing keystore.

## Step 1: Configure Signing

Run:

```
aroma sign
```

This process:

* Creates `android/keystore/release.keystore`
* Generates `android/keystore.properties`
* Updates `app/build.gradle` with signing configuration

The signing configuration is automatically connected to the Gradle `release` build type.

Important:

* Keep the keystore file secure
* Do not lose the password
* Do not commit the keystore to public repositories


## Step 2: Build Release APK

```
aroma build android --release
```

Signed APK output:

```
android/app/build/outputs/apk/release/app-release.apk
```

If unsigned, the output will show:

```
app-release-unsigned.apk
```

Unsigned APKs cannot be installed or distributed.


## Step 3: Build Android App Bundle (AAB)

For Google Play submission:

```
aroma build android --release --aab
```

Output:

```
android/app/build/outputs/bundle/release/app-release.aab
```

The AAB format is required for Play Store uploads.


# Manual Installation of Release APK

To install a signed release APK manually:

```
adb install -r android/app/build/outputs/apk/release/app-release.apk
```

If upgrading an existing version, ensure the keystore matches the previously installed version.


# Continuous Deployment Recommendations

For production workflows:

* Store keystore securely (offline backup)
* Use CI to run:

```
aroma build android --release --aab
```

* Archive generated AABs per version
* Tag releases in version control


# Play Store Deployment Checklist

Before uploading to Google Play:

* Version code incremented
* Version name updated
* Release build signed
* AAB generated
* App tested on physical device
* Debug logs removed
* Min SDK and Target SDK verified

Upload file:

```
app-release.aab
```


# Troubleshooting Deployment

## Keystore Password Incorrect

Verify with:

```
keytool -list -keystore release.keystore
```

If the password is wrong, you must recreate signing and cannot update existing published apps.

## No Device Detected

Check:

```
adb devices
```

Restart adb if needed:

```
adb kill-server
adb start-server
```

## SDK Not Found

Run:

```
aroma doctor
```

Or install manually:

```
aroma install-sdk
```


# Deployment Command Summary

Debug APK:

```
aroma build android
```

Release APK:

```
aroma build android --release
```

Release AAB:

```
aroma build android --release --aab
```

Run on device:

```
aroma run android
```

Run in emulator:

```
aroma run android --emu
```

Linux build:

```
aroma build linux
```

Linux run:

```
aroma run linux
```
