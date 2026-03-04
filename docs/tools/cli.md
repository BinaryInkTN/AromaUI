# CLI Reference

Complete reference for AromaUI command-line tools.

## Features
- Build automation
- Project scaffolding
- Utility commands

## Architecture

```
flowchart TD
    User[User] --> CLI[CLI Tool]
    CLI --> Build[Build]
    CLI --> Scaffold[Scaffold]
    CLI --> Utility[Utility]
```

## Example Usage
```sh
aroma build
aroma scaffold project
aroma util --help
```

## Commands
- aroma build
- aroma scaffold
- aroma util
