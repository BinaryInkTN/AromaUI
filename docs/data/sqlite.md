# SQLite Database

Embedded SQL database for structured data storage in AromaUI.

## Features
- SQL queries
- Transactions
- Data integrity

## Architecture

```
flowchart TD
    App[Application] --> DB[SQLite DB]
    DB --> Query[Query]
    DB --> Tx[Transaction]
```

## Example Usage
```c
aroma_db_open("app.db");
aroma_db_query("SELECT * FROM users;");
```

## API Reference
- aroma_db_open(path)
- aroma_db_query(sql)
- aroma_db_close(db)
