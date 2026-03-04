# Plugin Development

Extend AromaUI with custom plugins and modules.

## Features
- Plugin registration
- API extension
- Dynamic loading

## Architecture

```
flowchart TD
    App[Application] --> Plugin[Plugin]
    Plugin --> Register[Register]
    Plugin --> Extend[Extend API]
    Plugin --> Load[Dynamic Load]
```

## Example Usage
```c
aroma_plugin_register(plugin);
```

## API Reference
- aroma_plugin_register(plugin)
