# NFC API

Near-field communication (NFC) enables contactless data exchange. AromaUI provides APIs for reading, writing, and peer-to-peer communication.

## Features
- Tag reading/writing
- Peer-to-peer mode
- Secure transactions

## Architecture

```
flowchart TD
    Reader[Tag Reader] --> Read[Read Tag]
    Reader --> Write[Write Tag]
    Peer[Peer-to-Peer] --> Exchange[Data Exchange]
```

## Example Usage
```c
aroma_nfc_read();
aroma_nfc_write(tag_id, data);
```

## API Reference
- aroma_nfc_read()
- aroma_nfc_write(tag_id, data)
- aroma_nfc_exchange(peer_id, data)
