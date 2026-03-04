# USB Communication

AromaUI supports USB host and device mode for communication with peripherals.

## Features
- Device enumeration
- Data transfer
- Host/device mode switching
- Event handling

## Architecture

```
flowchart TD
    Host[USB Host] --> Enum[Enumerate Devices]
    Enum --> Transfer[Data Transfer]
    Device[USB Device] --> Transfer
```

## Example Usage
```c
aroma_usb_enumerate();
aroma_usb_send(device_id, data);
```

## API Reference
- aroma_usb_enumerate()
- aroma_usb_send(device_id, data)
- aroma_usb_set_mode(mode)
