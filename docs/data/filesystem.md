# File System API

AromaUI provides cross-platform file and directory operations for Android, Linux, and Windows.

## Features
- File read/write
- Directory management
- File metadata

## Architecture

```
flowchart TD
    App[Application] --> FS[File System]
    FS --> Read[Read/Write]
    FS --> Dir[Directory Management]
    FS --> Meta[Metadata]
```

## Example Usage
```c
aroma_fs_read("file.txt");
aroma_fs_write("file.txt", data);
aroma_fs_list_dir("/home/user");
```

## API Reference
- aroma_fs_read(path)
- aroma_fs_write(path, data)
- aroma_fs_list_dir(path)
