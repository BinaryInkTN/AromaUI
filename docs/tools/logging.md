The logging system in AromaUI is designed to provide developers with a flexible and efficient way to log messages, errors, and other information during the development and debugging process. The logging system supports multiple log levels, allowing developers to categorize their log messages based on their importance and severity. The available log levels include:

| Log Level       | Description                                      |
|-----------------|--------------------------------------------------|
| DEBUG_LEVEL_INFO            | General information about the application's state. |
| DEBUG_LEVEL_WARNING         | Indications of potential issues or important events. |
| DEBUG_LEVEL_ERROR           | Errors that have occurred in the application.      |
| DEBUG_LEVEL_CRITICAL        | Severe errors that may cause the application to crash. |

> Note: You can set a minimum log level to filter out less important messages. For example, setting the log level to WARNING will only log WARNING, ERROR, and CRITICAL messages, while INFO messages will be ignored. `set_minimum_log_level(LogLevel level)` can be used to set the minimum log level for the logging system. You can also enable or disable logging `set_logging_enabled(bool enabled);`.

### Logging Functions
The logging system provides a set of functions that developers can use to log messages at different log levels.

| Function Name       | Description                                      |
|---------------------|--------------------------------------------------|
| `LOG_INFO(const char* format, ...)` | Logs an informational message. |
| `LOG_WARNING(const char* format, ...)` | Logs a warning message. |
| `LOG_ERROR(const char* format, ...)` | Logs an error message. |
| `LOG_CRITICAL(const char* format, ...)` | Logs a critical error message. |

### Performance & Metrics
The logging system also includes a timer utility that allows developers to measure the time taken for specific operations or code blocks. This can be useful for performance profiling and optimization. The timer utility provides functions to start, stop, and reset timers, as well as retrieve the elapsed time in milliseconds.

| Function Name       | Description                                      |
|---------------------|--------------------------------------------------|
| `LOG_PERFORMANCE(NULL)` | Starts a performance timer. |
| `LOG_PERFORMANCE("Timer Name")` | Ends the specified performance timer. |

### Memory Debugging
In addition to logging messages and performance metrics, the logging system also includes a memory dump utility that allows developers to dump the contents of memory buffers for debugging purposes. This can be particularly useful for diagnosing issues related to memory management, such as buffer overflows or memory leaks. The memory dump utility provides a function to dump the contents of a specified memory buffer in a readable format.

| Function Name       | Description                                      |
|---------------------|--------------------------------------------------|
| `void dump_memory(const char *label, const void *buffer, size_t size);` | Dumps the contents of the specified memory buffer with a label for identification. |

### Exporting Logs
The logging system also provides functionality to export logs to a file for later analysis. This can be useful for debugging issues that occur in production environments or for keeping a record of application events. The log export function allows developers to specify the file path and name for the exported log file.

| Function Name       | Description                                      |
|---------------------|--------------------------------------------------|
| `void save_log_file(const char *path);` | Saves the current log messages to a file at the specified path. |

