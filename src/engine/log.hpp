#pragma once

namespace engine {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

// Call once at startup. file_path is optional; if set, also writes (without color) to that file.
void log_init(LogLevel min_level = LogLevel::Debug, const char* file_path = nullptr);
void log_shutdown();

// Internal — use the macros below.
void log_write(LogLevel level, const char* file, int line, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

} // namespace engine

#define LOG_DEBUG(...) engine::log_write(engine::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  engine::log_write(engine::LogLevel::Info,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  engine::log_write(engine::LogLevel::Warn,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) engine::log_write(engine::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
