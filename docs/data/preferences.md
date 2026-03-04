# Preferences API

Simple key-value storage for user preferences and settings.

## Features
- Store/retrieve preferences
- Data persistence
- Type support (int, string, bool)

## Architecture

```
flowchart TD
    App[Application] --> Prefs[Preferences]
    Prefs --> Store[Store Value]
    Prefs --> Retrieve[Retrieve Value]
```

## Example Usage
```c
aroma_prefs_set("theme", "dark");
aroma_prefs_get("theme");
```

## API Reference
- aroma_prefs_set(key, value)
- aroma_prefs_get(key)
