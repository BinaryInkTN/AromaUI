
AromaUI builds for three targets: Linux, Android, and Web. The `aroma` CLI wraps each toolchain. Run all commands inside your project folder.

## Commands

| Command | What it does |
|---|---|
| `python3 bin/aroma build linux` | Compile with CMake and Make |
| `python3 bin/aroma build android` | Build a debug APK with Gradle |
| `python3 bin/aroma build android --release` | Build a signed release APK or AAB |
| `python3 bin/aroma build web` | Compile to WASM with Emscripten |
| `python3 bin/aroma run linux` | Build and launch on Linux |
| `python3 bin/aroma run android --emu` | Install and launch on an emulator |
| `python3 bin/aroma run web` | Build for Web |

Check your setup first:

```bash
python3 bin/aroma doctor
```

## Linux build

```bash
python3 bin/aroma build linux
python3 bin/aroma run linux
```

The same steps by hand:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./MyApp
```

The binary name matches your project folder. If `run` cannot find it, open `build/` and launch the binary directly.

## Android build

You need JDK 17, an Android SDK platform, build tools, NDK, and CMake. The build asks you to pick versions the first time and saves them.

```bash
# Debug APK
python3 bin/aroma build android

# Signed release APK
python3 bin/aroma build android --release

# App Bundle for Google Play
python3 bin/aroma build android --aab
```

Run on a device or emulator:

```bash
python3 bin/aroma run android --emu
```

For release builds, set up your keystore in the Android project first. Gradle signs the output.

## Web build

You need the Emscripten SDK under `vendors/emscripten` or `~/.aroma/vendors/emscripten`.

```bash
python3 bin/aroma build web
python3 -m http.server 8080 --directory build_web
```

Open `http://localhost:8080` in your browser. Output goes to `build_web/`.

## Build flow

```
aroma build <target>
  linux:   CMake, then Make, then a native binary
  android: Gradle, then an APK or AAB
  web:     Emscripten with CMake, then WASM plus JS
```

## Common errors

* `Emscripten SDK not found`: install the SDK in one of the paths above, then retry.
* `No Android SDK platforms found`: install a platform with the SDK Manager and set `ANDROID_HOME`.
* `Not an Aroma project (no android/ directory)`: run the command inside a folder made by `aroma create`.
* `Executable not found. Run 'aroma build linux' first`: build before you run.

## What's next

* Learn [Project Creation](Project-Creation-and-Scaffolding.md) to scaffold a new app.
* Explore the [Widget Library](Input-and-Control-Widgets.md) to build your UI.
* Check [Testing and Quality](../Testing-and-Quality.md) for CI setup.
