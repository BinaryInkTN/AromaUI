# Profiling Tools

Performance profiling and memory leak detection in AromaUI.

## Features
- CPU/memory profiling
- Leak detection
- Performance reports

## Architecture

```
flowchart TD
    App[Application] --> Profiler[Profiler]
    Profiler --> CPU[CPU Profiling]
    Profiler --> Memory[Memory Profiling]
    Profiler --> Leak[Leak Detection]
```

## Example Usage
```c
aroma_profiler_start();
aroma_profiler_report();
```

## API Reference
- aroma_profiler_start()
- aroma_profiler_report()
