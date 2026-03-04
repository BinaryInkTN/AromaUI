# Migration Guide

Upgrade from older versions and migrate existing projects to AromaUI.

## Features
- Version compatibility
- Migration steps
- API changes

## Architecture

```
flowchart TD
    Old[Old Version] --> Migrate[Migrate]
    Migrate --> New[New Version]
    Migrate --> Update[Update API]
```

## Example Usage
```sh
aroma migrate --from 0.0.1 --to 0.1.0
```

## Steps
1. Review breaking changes
2. Update dependencies
3. Refactor code
