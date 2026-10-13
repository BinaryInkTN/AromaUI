
`aroma create` makes a new cross platform project in seconds, in C or in
Python. Run it from the AromaUI repo root.

## Creating a Project

```bash
python3 bin/aroma create MyApp
cd MyApp
```

Type a project name. Use a plain name with no spaces, such as `MyApp`.
The CLI then asks which language to use:

```
Language:
  1. C
  2. Python
Select language [1]:
```

Pick `1` (or press Enter) for a C project, `2` for Python. To skip the
question, pass the flag up front:

```bash
python3 bin/aroma create MyApp --python
```

## Generated Project Structure

A C project:

```
MyApp/
  src/main.c          # Entry point
  CMakeLists.txt      # Build config for Linux and Web
  android/            # Android project
    app/
      src/main/
        AndroidManifest.xml
        java/         # JNI bridge
        cpp/          # Native build config
```

A Python project (`--python` or option `2`) instead contains
`src/main.py` (your app: `build_ui()` + `teardown()`) plus `src/main.c`
(a fixed native bootstrap; do not put UI code there), a `requirements.txt`
for third-party packages, and the same
`android/` project with Chaquopy wired in. See
[Python on Android](Python-on-Android.md) for the full story.

## Starter Code

`src/main.c` is a full working app. It opens a window, shows text, and runs the main loop:

```c
int main(void) {
    aroma_ui_init();
    AromaWindow *window = aroma_ui_create_window("Hello World", 400, 600);
    // ... build UI ...
    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
    }
    aroma_ui_shutdown();
    return 0;
}
```

On Android, the template also wires `android_main()` to the NDK app state. You do not need to change this to start.

## Building the Project

```bash
python3 ../bin/aroma build linux
python3 ../bin/aroma run linux
```

Run Android and Web builds from the same folder. See [Building and Deployment](Building-and-Deployment.md).

## Next Steps

* Learn to [build for all platforms](Building-and-Deployment.md).
* Explore the [Widget Library](../widget-library/Input-and-Control-Widgets.md) to build your UI.
* Try [Incense](../widget-library/Incense-Sandbox.md) to prototype screens in your browser.
