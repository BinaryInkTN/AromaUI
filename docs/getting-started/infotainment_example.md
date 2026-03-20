| revision | author | date | description |
| --- | --- | --- | --- |
| 1 | AHMED ALI Yassine | 2026-03-19 | Initial creation of the Infotainment example documentation. |

---

The Infotainment example demonstrates how to create a simple infotainment system interface using AromaUI. It includes features such as media controls, navigation, and vehicle information display.

> This example is still a work in progress and will be updated with more features and improvements in the future.

### Main Dashboard
The main dashboard provides an overview of the vehicle's status, including speed, fuel level, climate control.
![Infotainment Example](./images/example_screenshot.png)

### Settings Page
The settings page allows users to customize their infotainment experience, including theme selection and display preferences.
![Settings Page](./images/example_screenshot_settings.png)

### Running the Example
To run the Infotainment example, follow these steps:

1. Clone the AromaUI repository and build the project.

```bash 

# Clone the repository
git clone https://github.com/BinaryInkTN/AromaUI.git

# Navigate to the project directory
cd AromaUI

# Build the project
cmake -S . -B build

# Navigate to the build directory
cd build

# Compile the project using all available CPU cores
make -j $(nproc)

```

2. Navigate to the examples directory and run the infotainment example.

```bash
# Navigate to the examples directory
cd examples/05_car_infotainment

# Build the example
cmake -S . -B build
cd build
make -j $(nproc)

# Run the example
./infotainment
```
