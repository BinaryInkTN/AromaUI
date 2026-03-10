> It is <b>highly discouraged</b> to directly interact with the AromaABI in most cases, as it is primarily intended for internal use within the Aroma UI framework. However, understanding the ABI can be beneficial for advanced users who wish to customize or extend the functionality of the framework. The ABI provides a standardized interface for interacting with the graphics and platform backends, allowing for greater flexibility and compatibility across different platforms and graphics APIs.

### Overview

The AromaABI is an interface that defines the functions and types necessary for interacting with the graphics and platform backends of the Aroma UI framework. It allows developers to set and get the types of graphics and platform backends being used, as well as retrieve the corresponding interfaces for each backend. This ABI is essential for ensuring compatibility and proper functioning of the Aroma UI across different platforms and graphics APIs.

### Functions

- `void (*set_graphics_backend_type)(AromaGraphicsBackendType type)`: Sets the type of graphics backend to be used by the Aroma UI framework.

- `void (*set_platform_backend_type)(AromaPlatformBackendType type)`: Sets the type of platform backend to be used by the Aroma UI framework.

- `AromaGraphicsBackendType (*get_graphics_backend_type)(void)`: Retrieves the current type of graphics backend being used by the Aroma UI framework.

- `AromaPlatformBackendType (*get_platform_backend_type)(void)`: Retrieves the current type of platform backend being used by the Aroma UI framework.

- `AromaGraphicsInterface* (*get_graphics_interface)(void)`: Retrieves a pointer to the graphics interface corresponding to the current graphics backend.

- `AromaPlatformInterface* (*get_platform_interface)(void)`: Retrieves a pointer to the platform interface corresponding to the current platform backend.

### Available Backends

- Graphics Backends:
    - `GRAPHICS_BACKEND_GLES3`: OpenGL ES 3.0 graphics backend.
    - `GRAPHICS_BACKEND_VULKAN`: Vulkan graphics backend.
    - `GRAPHICS_BACKEND_TFT_ESPI`: TFT_eSPI graphics backend for embedded systems. 

- Platform Backends:
    - `PLATFORM_BACKEND_GLPS`: GLPS platform backend for desktop applications.
    - `PLATFORM_BACKEND_ANDROID`: Android platform backend for mobile applications.
    - `PLATFORM_BACKEND_TFT_ESPI`: TFT_eSPI platform backend for embedded systems.

### Usage
To use the AromaABI, developers can include the `aroma_abi.h` header file in their application code. They can then set the desired graphics and platform backends using the provided functions, and retrieve the corresponding interfaces to interact with the backends. For example:


```c
#include "aroma_abi.h"

int main() {
    // Set the graphics and platform backends
    aroma_abi.set_graphics_backend_type(GRAPHICS_BACKEND_VULKAN);
    aroma_abi.set_platform_backend_type(PLATFORM_BACKEND_GLPS);

    // Retrieve the graphics and platform interfaces
    AromaGraphicsInterface* graphics_interface = aroma_abi.get_graphics_interface();
    AromaPlatformInterface* platform_interface = aroma_abi.get_platform_interface();

    // Use the interfaces to perform graphics and platform operations
    // ...

    return 0;
}
```



### Backend Compatibility Table
> ⚠ Note: The actual implementation of the functions and the behavior of the backends may vary based on the specific platform and graphics API being used. Developers should refer to the documentation for each backend for more details on how to properly utilize them in their applications. It is also discouraged to mix and match graphics and platform backends that are not designed to work together, as this may lead to compatibility issues or unexpected behavior. Always ensure that the chosen graphics and platform backends are compatible with each other and with the target platform for optimal performance and functionality.

| Graphics Backend       | Platform Backend        | Compatibility |
|-----------------------|------------------------|---------------|
| Vulkan                | GLPS                   | Yes           |
| Vulkan                | Android                | Yes           |
| Vulkan                | TFT_eSPI               | No            |
| OpenGL ES 3.0        | GLPS                   | Yes           |
| OpenGL ES 3.0        | Android                | Yes           |
| OpenGL ES 3.0        | TFT_eSPI               | No            |
| TFT_eSPI              | GLPS                   | No            |
| TFT_eSPI              | Android                | No            |
| TFT_eSPI              | TFT_eSPI               | Yes           | 

