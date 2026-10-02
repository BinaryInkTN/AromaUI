
`aroma create` makes a new cross platform project in seconds. Run it from the AromaUI repo root.

## Usage

```bash
python3 bin/aroma create MyApp
cd MyApp
```

Type a project name. Use a plain name with no spaces, such as `MyApp`.

## What you get

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

## Starter code

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

## Build it

```bash
python3 ../bin/aroma build linux
python3 ../bin/aroma run linux
```

Run Android and Web builds from the same folder. See [Building and Deployment](Building-and-Deployment.md).

## What's next

* Learn to [build for all platforms](Building-and-Deployment.md).
* Explore the [Widget Library](../widget-library/Input-and-Control-Widgets.md) to build your UI.
* Try [Incense](../widget-library/Incense-Sandbox.md) to prototype screens in your browser.
