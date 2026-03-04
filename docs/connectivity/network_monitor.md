# Network Monitoring

Monitor network connectivity, bandwidth, and signal strength in real-time.

## Features
- Connectivity status
- Bandwidth measurement
- Signal strength reporting

## Architecture

```
flowchart TD
    Status[Check Status] --> Bandwidth[Measure Bandwidth]
    Status --> Signal[Signal Strength]
```

## Example Usage
```c
aroma_net_status();
aroma_net_bandwidth();
aroma_net_signal_strength();
```

## API Reference
- aroma_net_status()
- aroma_net_bandwidth()
- aroma_net_signal_strength()
