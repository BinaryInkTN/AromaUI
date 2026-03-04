# Bluetooth LE API

Bluetooth Low Energy (LE) enables efficient communication with IoT devices and peripherals. AromaUI provides a cross-platform API for device discovery, connection, and data exchange.

## Features
- Device scanning and filtering
- Connection management
- GATT services and characteristics
- Notifications and data transfer

## Architecture
Bluetooth LE API is structured around device scanning, connection, and GATT operations.

Mermaid diagram:

```
flowchart TD
    Scan[Scan Devices] --> Connect[Connect Device]
    Connect --> GATT[GATT Operations]
    GATT --> Notify[Notifications]
    GATT --> Read[Read Characteristic]
    GATT --> Write[Write Characteristic]
```

## Example Usage
```c
// Scan for BLE devices
aroma_ble_scan();

// Connect to a device
aroma_ble_connect(device_id);

// Read a characteristic
aroma_ble_read(device_id, service_uuid, char_uuid);
```

## API Reference
- aroma_ble_scan()
- aroma_ble_connect(device_id)
- aroma_ble_read(device_id, service_uuid, char_uuid)
- aroma_ble_write(device_id, service_uuid, char_uuid, data)
- aroma_ble_subscribe(device_id, service_uuid, char_uuid, callback)
