# AromaUI

AromaUI is a modern, lightweight C UI framework designed for embedded and desktop applications. It provides a set of customizable widgets and tools to build beautiful, responsive user interfaces in C.

![AromaUI Hero](/images/hero.png)

## Features
- Modular widget system
- Material design components
- Multi-window support
- Customizable themes
- Efficient rendering
- Easy integration with existing C projects

## Getting Started

### Prerequisites
- C compiler (GCC/Clang)
- CMake 3.10+
- Linux (other platforms coming soon)

### Build Instructions
```sh
mkdir build
cd build
cmake ..
make
```

### Running Examples
Navigate to any example folder and build:
```sh
cd examples/01_hello_button
mkdir build && cd build
cmake ..
make
./hello_button
```

## Example: Settings App
AromaUI comes with a sample settings application to demonstrate its capabilities.

![Settings App Screenshot](/images/settings_app.png)

To run:
```sh
cd examples/settings_app/build
./settings_app
```

## Directory Structure
- `src/` - Core library source code
- `include/` - Public headers
- `examples/` - Example applications
- `tests/` - Unit tests
- `vendors/` - Third-party dependencies
- `images/` - Project images and screenshots

## Contributing
Contributions are welcome! Please open issues or pull requests to help improve AromaUI.

## License
AromaUI is released under the MIT License.
