# Wi-Fi Direct

Wi-Fi Direct enables peer-to-peer connections between devices without an access point. AromaUI abstracts connection setup and data transfer.

## Features
- Device discovery
- Group formation
- Secure connections
- Data exchange

## Architecture

```
flowchart TD
    Discover[Discover Devices] --> Group[Form Group]
    Group --> Connect[Connect Peers]
    Connect --> Transfer[Data Transfer]
```

## Example Usage
```c
aroma_wifi_direct_discover();
aroma_wifi_direct_connect(peer_id);
aroma_wifi_direct_send(peer_id, data);
```

## API Reference
- aroma_wifi_direct_discover()
- aroma_wifi_direct_connect(peer_id)
- aroma_wifi_direct_send(peer_id, data)
