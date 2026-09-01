
`aroma create` initializes a new cross-platform project in seconds.

## Usage

```bash
python3 bin/aroma create MyApp
cd MyApp
```

The CLI prompts for a project name and package name (e.g., `com.example.myapp`).

## What Gets Generated

```
MyApp/
  src/main.c          # Entry point (Linux/Android/Web)
  CMakeLists.txt      # Root build config
  android/            # Android Studio project
    app/
      src/main/
        AndroidManifest.xml
        java/         # JNI bridge (AromaHelper.java)
        cpp/          # Native CMake config
```

## Generated Entry Point

The `main.c` template shows the standard AromaUI lifecycle:

```c
int main(int argc, char **argv) {
    AromaUIState state;
    aroma_ui_init(&state);
    AromaNode *window = aroma_ui_create_window(&state, 800, 480, "MyApp");
    // ... build UI ...
    while (aroma_ui_is_running()) {
        aroma_ui_process_events(&state);
        aroma_ui_render(window);
    }
    aroma_ui_shutdown(&state);
    return 0;
}
```

For Android, the template includes `android_main()` which bridges the NDK app state to the AromaUI platform backend.

## Build After Creation

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./MyApp
```

## What's Next

- Learn to [build for all platforms](Building-and-Deployment.md).
- Explore the [Widget Library](Input-and-Control-Widgets.md) to build your UI.
- Try [Incense](../widget-library/wasm/incense_sandbox/index.html) for rapid prototyping.
