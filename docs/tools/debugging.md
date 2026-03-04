# Debugging Guide

Tools and techniques for debugging AromaUI applications.

## Features
- Breakpoint support
- Logging and tracing
- Error reporting

## Architecture

```
flowchart TD
    App[Application] --> Debugger[Debugger]
    Debugger --> Breakpoint[Breakpoints]
    Debugger --> Log[Logging]
    Debugger --> Error[Error Reporting]
```

## Example Usage
```c
aroma_debug_set_breakpoint(line);
aroma_log("Debug message");
```

## API Reference
- aroma_debug_set_breakpoint(line)
- aroma_log(message)
