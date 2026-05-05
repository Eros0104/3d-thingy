#include "engine/log.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace engine {

namespace {

LogLevel g_min_level = LogLevel::Debug;
FILE*    g_file      = nullptr;

const char* level_tag(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info:  return "INF";
        case LogLevel::Warn:  return "WRN";
        case LogLevel::Error: return "ERR";
        default:              return "???";
    }
}

const char* level_color(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "\033[2m";     // dim
        case LogLevel::Info:  return "\033[36m";    // cyan
        case LogLevel::Warn:  return "\033[33m";    // yellow
        case LogLevel::Error: return "\033[1;31m";  // bold red
        default:              return "\033[0m";
    }
}

static constexpr const char* k_reset = "\033[0m";

const char* basename(const char* path) {
    const char* last = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\') last = p + 1;
    return last;
}

void timestamp(char* buf, size_t sz) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const time_t t = system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    snprintf(buf, sz, "%02d:%02d:%02d.%03d",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count());
}

} // namespace

void log_init(LogLevel min_level, const char* file_path) {
    g_min_level = min_level;
    if (g_file) { fclose(g_file); g_file = nullptr; }
    if (file_path) {
        g_file = fopen(file_path, "w");
        if (!g_file)
            fprintf(stderr, "log: failed to open '%s'\n", file_path);
    }
}

void log_shutdown() {
    if (g_file) { fclose(g_file); g_file = nullptr; }
}

void log_write(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (level < g_min_level) return;

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char ts[16];
    timestamp(ts, sizeof(ts));

    const char* name  = basename(file);
    const char* tag   = level_tag(level);
    const char* color = level_color(level);

    fprintf(stderr, "%s[%s] %s %s:%d: %s%s\n", color, tag, ts, name, line, msg, k_reset);

    if (g_file) {
        fprintf(g_file, "[%s] %s %s:%d: %s\n", tag, ts, name, line, msg);
        fflush(g_file);
    }
}

} // namespace engine
