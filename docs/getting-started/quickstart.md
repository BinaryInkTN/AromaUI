<b>Author: AHMED ALI Mohamed Yassine</b>

<br/>

# Getting Started with AromaUI

This guide walks you through creating, building, and running your first AromaUI application using the `aroma` CLI.

AromaUI supports:

* Linux (native desktop applications)
* Android (Debug and Release builds)

All commands must be executed from the root of your AromaUI project.




# 3. Build for Linux

## Build

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

## Run

```
aroma run linux
```

This builds (if needed) and launches the application.


# 4. Build for Android (Debug)

## Build Debug APK

```
aroma build android
```

Output:

```
android/app/build/outputs/apk/debug/app-debug.apk
```

## Install on Connected Device

```
aroma run android
```

Requirements:

* USB debugging enabled
* `adb devices` shows a connected device

## Run in Emulator

```
aroma run android --emu
```

This will:

* Create an AVD if needed
* Start the emulator
* Install the debug APK
* Launch the application


# 5. Create a Release Build

## Configure Signing

```
aroma sign
```

This creates:

* `android/keystore/release.keystore`
* `android/keystore.properties`

Keep the keystore secure and never commit it to public repositories.

## Build Release APK

```
aroma build android --release
```

Output:

```
android/app/build/outputs/apk/release/app-release.apk
```

## Build Android App Bundle (AAB)

```
aroma build android --release --aab
```

Output:

```
android/app/build/outputs/bundle/release/app-release.aab
```


# 6. Verify Environment

If something fails, check your environment:

```
aroma doctor
```

Install missing SDK components if required:

```
aroma install-sdk
```


# 7. Next Steps

After successfully building and running your application:

* Start building UI layouts (windows, containers, labels, buttons)
* Apply themes
* Add event handlers
* Prepare signed release builds for distribution

You now have a working AromaUI project ready for development and deployment.
