
AromaUI supports three platforms: Linux, Android, and Web (WASM). The `aroma` CLI abstracts the build toolchain for each.

## Commands

| Command | Action |
|---|---|
| `aroma build linux` | Native compilation via CMake + Make |
| `aroma build android` | Gradle build (Debug APK) |
| `aroma build android --release` | Signed release APK/AAB |
| `aroma build web` | Emscripten → WASM + HTML |
| `aroma run linux` | Build and run on Linux |
| `aroma run android --emu` | Build, install, and run on AVD |
| `aroma run web` | Build and open in browser |

## Environment Setup

```bash
# Validate dependencies
python3 bin/aroma doctor

# Install Android SDK if needed
python3 bin/aroma install-sdk
```

## Linux Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./MyApp
```

## Android Build

```bash
# Debug
python3 ../bin/aroma build android

# Release (requires signing)
python3 ../bin/aroma build android --release

# App Bundle for Google Play
python3 ../bin/aroma build android --aab
```

Signing requires a keystore. Generate one with:

```bash
python3 bin/aroma sign
```

## Web Build

```bash
python3 bin/aroma build web
# Output in build-web/
python3 -m http.server 8080 --directory build-web
```

## Build Pipeline

```
aroma build <target>
  → Target dispatch
    → linux:   CMake → Make → binary
    → android: Gradle → APK/AAB
    → web:     Emscripten + CMake → .wasm + .js
```

## What's Next

- Learn [Project Creation](Project-Creation-and-Scaffolding.md) to scaffold a new app.
- Explore the [Widget Library](Input-and-Control-Widgets.md) to build your UI.
- Check [Testing & Quality](Testing-and-Quality.md) for CI integration.
