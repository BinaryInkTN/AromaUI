# Network Security

SSL/TLS, certificate pinning, and secure networking in AromaUI.

## Features
- SSL/TLS support
- Certificate pinning
- Secure sockets

## Architecture

```
flowchart TD
    Connect[Connect] --> SSL[SSL/TLS]
    SSL --> Pin[Certificate Pinning]
    SSL --> Socket[Secure Socket]
```

## Example Usage
```c
aroma_ssl_connect(host, port);
aroma_ssl_set_pinning(cert);
```

## API Reference
- aroma_ssl_connect(host, port)
- aroma_ssl_set_pinning(cert)
- aroma_ssl_socket(host, port)
