# Logging System

Comprehensive logging with multiple verbosity levels in AromaUI.

## Features
- Verbosity control
- Log filtering
- Persistent logs

## Architecture

```
flowchart TD
    App[Application] --> Logger[Logger]
    Logger --> Verbosity[Verbosity]
    Logger --> Filter[Filter]
    Logger --> Persist[Persist Logs]
```

## Example Usage
```c
aroma_log_set_level(DEBUG);
aroma_log("App started");
```

## API Reference
- aroma_log_set_level(level)
- aroma_log(message)
