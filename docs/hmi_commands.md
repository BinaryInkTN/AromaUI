# HMI Commands

This document lists all the available HMI (Human-Machine Interface) commands in the AromaOS CAN Bus Simulator.

| Command Name | CAN ID | Param 1 | Param 2 | Description |
|---|---|---|---|---|
| Nav Home | 0x01 | 0x00 | 0x00 | Navigate to home address |
| Nav Work | 0x01 | 0x01 | 0x00 | Navigate to work address |
| Radio FM | 0x02 | 0x01 | 0x00 | Switch to FM radio |
| Radio AM | 0x02 | 0x02 | 0x00 | Switch to AM radio |
| Bluetooth | 0x02 | 0x03 | 0x00 | Switch to Bluetooth audio |
| Screen Off | 0x03 | 0x00 | 0x00 | Turn off centre display |
| Screen On | 0x03 | 0x01 | 0x00 | Turn on centre display |
| Night Mode | 0x04 | 0x01 | 0x00 | Enable night/dark mode |
| Day Mode | 0x04 | 0x00 | 0x00 | Enable day/light mode |
| Fan + | 0x05 | 0x01 | 0x00 | HVAC fan speed up |
| Fan - | 0x05 | 0xFF | 0x00 | HVAC fan speed down |
| Defrost | 0x06 | 0x01 | 0x00 | Toggle rear defrost |

These commands are sent exactly on `0x300` when triggered from the HMI Commands panel.

