# AromaUI IDE Example

A full-featured IDE for building AromaUI interfaces with the Incense declarative language. Write code on the left, see live previews on the right via WebAssembly.

## Features

- **Monaco Editor**: Syntax highlighting, autocomplete, and hover docs for the Incense language
- **Live Preview**: Canvas-based preview that updates instantly when you click Run
- **Example Files**: Pre-built examples showcasing different widget categories
- **Theme Toggle**: Light and dark themes
- **Status Bar**: Real-time error reporting and status

## Open the IDE

**[Launch AromaUI IDE](ide_example/index.html)**

## Usage

1. Edit the Incense code in the left panel
2. Click **Run** (or press `Ctrl+Enter`) to compile and preview
3. Choose from example files using the dropdown
4. Toggle theme with the sun/moon button

## Example Code

```aroma
Window {
    width: 320
    height: 480
    title: "Settings"

    Container {
        id: "page_main"
        x: 0
        y: 0
        width: 320
        height: 480
        layout: flex
        direction: column
        visible: 1

        Container {
            x: 0
            y: 0
            width: 320
            height: 120
            color: #121212

            IconButton {
                x: 8
                y: 8
                width: 40
                height: 40
                icon: "AROMA_ICON_ARROW_BACK"
                variant: standard
                on_click: "back"
                visible: 0
            }

            Label {
                text: "Settings"
                style: large
                color: #FFFFFF
                y: 20
                x: 10
            }

            Textbox {
                text: ""
                placeholder: "Search settings"
                x: 10
                y: 56
                width: 300
                height: 44
                on_change: "on_search_change"
            }
        }

        ListView {
            id: "settings_list"
            x: 0
            y: 120
            width: 320
            height: 360
            on_select: "navigate"

            Header { text: "General" }
            ListItem { text: "Airplane Mode" secondary: "Off" icon: "AROMA_ICON_AIRPLANEMODE_INACTIVE" }
            ListItem { text: "Wi-Fi" secondary: "Not Connected" icon: "AROMA_ICON_NETWORK_WIFI" }
            ListItem { text: "Bluetooth" secondary: "On" icon: "AROMA_ICON_BLUETOOTH" }
            ListItem { text: "Cellular" secondary: "5G On" icon: "AROMA_ICON_SIGNAL_CELLULAR_4_BAR" }
            ListItem { text: "VPN" secondary: "Disconnected" icon: "AROMA_ICON_LOCK" }

            Header { text: "Display & Brightness" }
            ListItem { text: "Brightness" secondary: "50%" icon: "AROMA_ICON_BRIGHTNESS_MEDIUM" }
            ListItem { text: "Auto-Lock" secondary: "2 minutes" icon: "AROMA_ICON_LOCK" }
            ListItem { text: "Night Shift" secondary: "On until 7 AM" icon: "AROMA_ICON_WB_SUNNY" }
            ListItem { text: "True Tone" secondary: "On" icon: "AROMA_ICON_BRIGHTNESS_AUTO" }

            Header { text: "Privacy" }
            ListItem { text: "Location Services" secondary: "While Using the App" icon: "AROMA_ICON_LOCATION_CITY" }
            ListItem { text: "Photos" secondary: "All Photos" icon: "AROMA_ICON_PHOTO" }
            ListItem { text: "Camera" secondary: "On" icon: "AROMA_ICON_PHOTO_CAMERA" }
            ListItem { text: "Microphone" secondary: "On" icon: "AROMA_ICON_MIC" }
        }
    }
}
```

## Search Feature

The Settings UI includes a search Textbox that filters the ListView in real-time. Typing in the search box triggers the `on_search_change` callback, which uses `aroma_listview_set_item_hidden()` to hide non-matching items.

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl/Cmd + Enter` | Run code |
| `Ctrl/Cmd + Space` | Trigger autocomplete |

## Building Locally

```bash
cd examples/ide_example
cmake -B build_web -DCMAKE_TOOLCHAIN_FILE=../../vendors/emscripten/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
cmake --build build_web
```
