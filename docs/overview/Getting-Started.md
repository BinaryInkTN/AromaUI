
Start here. On Linux, the fastest path is the GUI installer. No cloning, no toolchain setup.

## Recommended: download the GUI installer for Linux

[Download aroma-lavender-linux.tar.xz (v0.0.2)](https://github.com/BinaryInkTN/AromaUI/releases/download/v0.0.2/aroma-lavender-linux.tar.xz)

Or get it step by step:

1. Open the [releases page](https://github.com/BinaryInkTN/AromaUI/releases).
2. Download the Linux installer archive (`aroma-lavender-linux.tar.xz`).
3. Extract it:
   ```bash
   tar -xf aroma-lavender-linux.tar.xz
   ```
4. Run the installer, pick the components you want, and follow the screens.

What you need for the installer to run:

* GLIBC 2.27 or newer (check with `ldd --version`)
* OpenGL 2.1 or newer, or OpenGL ES 2.0 with EGL
* X11 (on Wayland it runs through XWayland)

If the installer does not start, check those three items first, then grab the newest release from the releases page.

## Option A: Try Incense in your browser (no install)

1. Open `docs/sandbox.html` in this repo. You can also open the live sandbox from the docs home page.
2. Edit the code on the left.
3. Click Run.
4. The phone preview on the right updates at once.

Start with this sample:

```aroma
Window {
    width: 320
    height: 480
    title: "My App"

    Container {
        x: 0
        y: 0
        width: 320
        height: 480
        layout: flex
        direction: column

        Label {
            text: "Hello"
            style: large
        }

        Button {
            text: "Click Me"
            x: 20
            y: 100
            width: 120
            height: 40
            on_click: "handle_click"
        }
    }
}
```

If the preview stays blank, click Run again and check the status bar at the bottom. It shows the exact line with the error.

## Option B: Build from source on Linux

Use this path if you want the latest code or you plan to change the framework itself. Otherwise the installer above is quicker.

### What you need

* Linux with Git, CMake 3.22.1 or newer, and Python 3.8 or newer
* A C compiler such as GCC or Clang
* Optional for Android builds: JDK 17 plus Android SDK, NDK, and CMake

Check your tools first:

```bash
cmake --version
python3 --version
gcc --version
```

### 1. Clone the repo

```bash
git clone https://github.com/BinaryInkTN/AromaUI.git --recursive
cd AromaUI
```

You need `--recursive`. It pulls in FreeType and other vendored deps.

### 2. Check your setup

```bash
python3 bin/aroma doctor
```

Read the output. Fix any item marked FAIL before you go on. For Android, install the SDK and NDK with Android Studio or the command line tools, then run `doctor` again.

### 3. Create a project

```bash
python3 bin/aroma create MyApp
cd MyApp
```

You get this layout:

* `src/main.c`: app entry point
* `CMakeLists.txt`: build config for Linux and Web
* `android/`: Android project with Gradle and JNI bridge

### 4. Build and run on Linux

Run this inside your new `MyApp` folder:

```bash
python3 ../bin/aroma build linux
python3 ../bin/aroma run linux
```

Or build by hand:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./MyApp
```

A window opens with "Hello, World!" and a counter that updates each second. If the build fails, see Common errors below.

### 5. Build for Web

You need the Emscripten SDK under `vendors/emscripten` or `~/.aroma/vendors/emscripten`.

```bash
python3 ../bin/aroma build web
python3 -m http.server 8080 --directory build_web
```

Open `http://localhost:8080` in your browser.

### 6. Build for Android

You need a working SDK, NDK, and an emulator or a plugged in device.

```bash
python3 ../bin/aroma build android
python3 ../bin/aroma run android --emu
```

Use `--release` or `--aab` only for store builds.

## Your first C app

`aroma create` gives you this working program in `src/main.c`:

```c
#include <aroma.h>
#include <unistd.h>

int main(void)
{
    aroma_ui_init();

    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);

    AromaWindow *window = aroma_ui_create_window("Hello World", 400, 600);

    AromaFont *font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf,
        aroma_ubuntu_ttf_len,
        24);

    AromaNode *root = aroma_ui_container(
        (AromaNode *)window,
        0, 0, 400, 600,
        AROMA_LAYOUT_MODE_FLEX,
        AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER,
        AROMA_ALIGN_CENTER);

    aroma_ui_label(
        root,
        "Hello, World!",
        0, 0,
        LABEL_STYLE_LABEL_LARGE,
        font);

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(1000000);
    }

    aroma_font_destroy(font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();

    return 0;
}
```

The pattern is always the same:

1. Call `aroma_ui_init()`.
2. Create a window.
3. Add nodes such as containers and labels.
4. Loop with `aroma_ui_process_events()` and `aroma_ui_render()`.
5. Clean up with `aroma_ui_shutdown()`.

## Common errors

* `cmake: command not found`: install CMake, then open a new terminal.
* Empty clone or missing headers: you cloned without `--recursive`. Run `git submodule update --init --recursive`.
* `Emscripten SDK not found`: place the SDK under `vendors/emscripten` or `~/.aroma/vendors/emscripten`, then retry `build web`.
* `No Android SDK platforms found`: set `ANDROID_HOME` or install platforms with the SDK Manager, then retry.
* `Not an Aroma project (no android/ directory)`: run Android commands inside the folder created by `aroma create`.
* Sandbox shows an error: read the status bar. Fix the line it points to, then click Run.

## What's next

* Read [Architecture Overview](Architecture-Overview.md) to learn how nodes, layout, and rendering fit together.
* Browse [Input and Controls](../widget-library/Input-and-Control-Widgets.md) for buttons, text, sliders, and lists.
* Open [Incense Declarative UI](../widget-library/Incense-Sandbox.md) to prototype screens fast, then port them to C.
* See [Building and Deployment](../cli-toolchain/Building-and-Deployment.md) for Linux, Android, and Web builds in full detail.
