# Testing Framework

Unit testing, integration testing, and UI testing tools for AromaUI.

## Features
- Unit and integration tests
- UI automation
- Test reporting

## Architecture

```
flowchart TD
    Test[Tests] --> Unit[Unit Test]
    Test --> Integration[Integration Test]
    Test --> UI[UI Test]
    Test --> Report[Report]
```

## Example Usage
```c
aroma_test_run("test_suite");
```

## API Reference
- aroma_test_run(suite)
- aroma_test_report()
