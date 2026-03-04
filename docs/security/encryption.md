# Encryption API

AromaUI supports AES and RSA encryption for secure data storage and transmission.

## Features
- AES symmetric encryption
- RSA asymmetric encryption
- Key management

## Architecture

```
flowchart TD
    Data[Data] --> Encrypt[Encrypt]
    Data --> Decrypt[Decrypt]
    Encrypt --> Key[Key Management]
```

## Example Usage
```c
aroma_encrypt_aes(data, key);
aroma_decrypt_aes(data, key);
```

## API Reference
- aroma_encrypt_aes(data, key)
- aroma_decrypt_aes(data, key)
- aroma_encrypt_rsa(data, pubkey)
- aroma_decrypt_rsa(data, privkey)
