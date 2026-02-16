# Getting Started with AromaUI CLI

AromaUI CLI is a command-line tool for creating, building, and running cross-platform C applications using AromaUI. It supports:

* Linux (native desktop builds using CMake)
* Android (NDK + NativeActivity via Gradle)

This guide walks you through installation, project creation, building, and running your first application.

---

# 1. Prerequisites

## Linux Requirements

Make sure the following tools are installed:

* GCC
* CMake
* Make
* Ninja (optional but recommended)
* Java (required for Android builds)

You can verify your environment using:

```
aroma doctor
```

<img src="images/aroma_doctor.png" width="800"/>

The doctor command checks:

* CMake installation
* Ninja installation
* GCC availability
* Android SDK and NDK setup

If Android SDK or NDK is missing, the tool can install it automatically.

---

# 2. Installing Android SDK & NDK (Optional)

If you plan to build for Android, you must install:

* Android SDK
* Android NDK
* Platform tools
* Build tools
* CMake (Android version)

You can install everything automatically:

```
aroma install-sdk
```

Or allow the CLI to install it automatically during:

```
aroma doctor
```

or

```
aroma build android
```

The SDK will be installed in:

```
~/Android/Sdk
```

unless ANDROID_HOME is already defined.

---

# 3. Creating a New Project

To create a new project:

```
aroma create MyApp
```

If no name is provided:

```
aroma create
```

You will be prompted for:

* Project name
* Android package name (e.g. com.example.myapp)
* Minimum SDK version
* Target SDK version
* Compile SDK version

<img src="images/aroma_create.png" width="800"/>


## Project Structure

After creation:

```
MyApp/
├── CMakeLists.txt
├── src/
│   ├── main.c
│   └── logo.h
└── android/
    ├── app/
    ├── gradlew
    └── build.gradle
```

The project is immediately ready to build.

---

# 4. Building the Project

Navigate into your project directory:

```
cd MyApp
```

## Build for Linux

```
aroma build linux
```

This will:

* Create a build/ directory
* Run CMake configuration
* Compile using make

Output binary will be inside:

```
build/
```

---

## Build for Android

```
aroma build android
```

This will:

* Validate SDK/NDK
* Configure Gradle project
* Build debug APK

Generated APK location:

```
android/app/build/outputs/apk/debug/app-debug.apk
```

---

# 5. Running the Project

## Run on Linux

```
aroma run linux
```

This will:

* Build the project
* Locate the executable
* Launch it automatically

---

## Run on Android (Connected Device)

Connect a device via USB with USB debugging enabled, then:

```
aroma run android
```

This will:

* Build the APK
* Install it using adb
* Launch the NativeActivity

---

## Run on Android Emulator

You can automatically create and launch an emulator:

```
aroma run android --emu
```

This will:

* Install missing emulator components
* Create an AVD if needed
* Boot the emulator
* Wait for full boot completion
* Install and launch the app

---

# 6. Useful Commands Overview

Check environment:

```
aroma doctor
```

Install Android SDK & NDK:

```
aroma install-sdk
```

Create new project:

```
aroma create MyApp
```

Build project:

```
aroma build linux
aroma build android
```

Run project:

```
aroma run linux
aroma run android
aroma run android --emu
```

---

# 7. Environment Variables

The following variables may be used:

ANDROID_HOME
ANDROID_NDK_HOME
JAVA_HOME

If not defined, the CLI attempts to auto-detect common locations.

---

# 8. Troubleshooting

If builds fail:

1. Run:

   ```
   aroma doctor
   ```

2. Ensure Java is installed.

3. Ensure ANDROID_HOME is correctly set.

4. Verify adb works:

   ```
   adb devices
   ```

5. If Android SDK is missing, run:

   ```
   aroma install-sdk
   ```

# 9. Write your android app in pure C!

<div style="text-align:center;  display: flex; flex-wrap:wrap;">


<!-- Scrollable Code -->
<div style="text-align:left; flex: 2; max-height: 600px; overflow: auto;padding: 10px; ">
```c
/**
 * 
 * AromaUI minimal example
 * 
 */

#include "aroma.h"
#include "logo.h"
#include <stdio.h>
#include <unistd.h>

static bool on_btn_click(AromaNode* btn, void* data)
{
    aroma_ui_android_intent(AROMA_INTENT_VIEW,
        "https://github.com/BinaryInkTN/AromaUI", NULL, NULL, 0);
    
    return true;
}

int main(int argc, char** argv)
{
    if (!aroma_ui_init()) {
        return 1;
    }

    AromaTheme theme = aroma_theme_create_material_black();
    aroma_ui_set_theme(&theme);

    AromaWindow* win = aroma_ui_create_window("AromaUI Showcase", 400, 800);
    aroma_window_set_fullscreen((AromaNode*)win, true);
    
    AromaFont* font_md = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 48);
    
    int w, h;
    aroma_window_get_size(win, &w, &h);
    
    AromaContainer* root_container = aroma_ui_container(
        (AromaNode*)win, 0, 0, w, h,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    
    aroma_node_set_gap((AromaNode*)root_container, 40);
    
    aroma_ui_image_mem((AromaNode*)root_container,
        leaf_png, leaf_png_len, 100, 50, 256, 256);
    
    aroma_ui_divider((AromaNode*)win, 0, 150, w, DIVIDER_ORIENTATION_HORIZONTAL);
    
    aroma_ui_label((AromaNode*)win, "Minimal App", 40, 50,
        LABEL_STYLE_LABEL_LARGE, font_md);
    
    AromaLabel* label = aroma_ui_create_label(
        (AromaNode*)root_container, "Hello, AromaUI!", 20, 20,
        LABEL_STYLE_LABEL_LARGE);
    aroma_label_set_font(label, font_md);
    
    AromaLabel* description = aroma_ui_create_label(
        (AromaNode*)root_container,
        "Press to visit our GitHub repository!", 20, 20,
        LABEL_STYLE_LABEL_SMALL);
    aroma_label_set_font((AromaNode*)description, font_md);
    aroma_ui_button((AromaNode*)root_container, "Let's go!", 20, 20,
        230, 100, on_btn_click, NULL, font_md);
    
    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(win);
        usleep(16000);
    }
    
    aroma_ui_shutdown();
    return 0;
}

#ifdef __ANDROID__
#include <android_native_app_glue.h>
void android_main(struct android_app* state)
{
    aroma_android_set_app(state);
    main(0, NULL);
}
#endif
```


</div>

<!-- Preview Image -->
<div style="flex: 1; max-width: 40%;">
    <img src="images/minimal_example.png" style="max-width:300px; " />
</div>
</div>


* Modify src/main.c to build your UI.
* Extend CMakeLists.txt if needed.
* Customize the Android Gradle project inside android/.

Your AromaUI project is now ready for cross-platform development.
