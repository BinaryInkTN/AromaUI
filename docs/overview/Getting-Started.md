
Get up and running with AromaUI in under five minutes. This guide covers cloning, building, and running your first cross-platform app.

## Prerequisites

- **Linux** (primary development host)
- **CMake** 3.22.1+
- **Python** 3.8+
- **Git** with submodule support
- Optional: **Java JDK** 17, **Android SDK/NDK** for mobile builds

## 1. Clone the Repository

```bash
git clone https://github.com/BinaryInkTN/AromaUI.git --recursive
cd AromaUI
```

The `--recursive` flag is required. AromaUI vendors FreeType, GLES headers, and other dependencies as git submodules.

## 2. Validate Your Environment

```bash
python3 bin/aroma doctor
```

This checks for CMake, GCC/Clang, Java, and Android SDK/NDK. If Android components are missing:

```bash
python3 bin/aroma install-sdk
```

## 3. Create a Project

```bash
python3 bin/aroma create MyApp
cd MyApp
```

This scaffolds a cross-platform project with:
- `src/main.c` - application entry point
- `CMakeLists.txt` - Linux/Web build config
- `android/` - Android Studio project with JNI bridge

## 4. Build and Run

### Linux

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./MyApp
```

### Web (WASM)

```bash
python3 ../bin/aroma build web
# Output in build-web/
python3 -m http.server 8080 --directory build-web
# Open http://localhost:8080
```

### Android

```bash
python3 ../bin/aroma run android --emu
```

Compiles via NDK, builds the APK with Gradle, and deploys to an Android Virtual Device.

## 5. Your First AromaUI App

A minimal app follows this pattern:

```c
#include <aroma_ui.h>

int main(int argc, char **argv)
{
    AromaUIState state;
    aroma_ui_init(&state);

    AromaNode *window = aroma_ui_create_window(&state, 800, 480, "Hello AromaUI");
    AromaNode *root = aroma_ui_get_root(window);

    AromaNode *btn = aroma_ui_button(root, "Click Me", 20, 20, 160, 48);
    aroma_button_set_on_click(btn, [](AromaNode *node, void *ud) {
        aroma_label_set_text((AromaNode *)ud, "Clicked!");
    }, NULL);

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events(&state);
        aroma_ui_render(window);
    }

    aroma_ui_shutdown(&state);
    return 0;
}
```

## What's Next

- Read [Architecture Overview](Architecture-Overview.md) to understand the framework design.
- Explore [Widget Library](Layout-and-Navigation-Widgets.md) to learn the available UI components.
- Try the [Incense Sandbox](../widget-library/wasm/incense_sandbox/index.html) for rapid UI prototyping.
