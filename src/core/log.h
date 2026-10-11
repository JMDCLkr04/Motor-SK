#pragma once

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>

namespace sk {

enum class LogLevel { Info, Warn, Error };

// Destino adicional de los mensajes (ademas de stdout): lo usa el panel
// Output de PLACE para mostrar SK_INFO/WARN/ERROR. El mismo objeto se
// comparte entre unidades de traduccion (static local de funcion inline).
inline std::function<void(LogLevel, const std::string&)>& logSink() {
    static std::function<void(LogLevel, const std::string&)> sink;
    return sink;
}

inline void setLogSink(std::function<void(LogLevel, const std::string&)> sink) {
    logSink() = std::move(sink);
}

inline void log(LogLevel level, const char* fmt, ...) {
    const char* tag = "INFO ";
    if (level == LogLevel::Warn) tag = "WARN ";
    if (level == LogLevel::Error) tag = "ERROR";

    char body[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    std::printf("[%s] %s\n", tag, body);
    std::fflush(stdout);

    if (logSink()) logSink()(level, std::string(body));
}

} // namespace sk

#define SK_INFO(...)  ::sk::log(::sk::LogLevel::Info,  __VA_ARGS__)
#define SK_WARN(...)  ::sk::log(::sk::LogLevel::Warn,  __VA_ARGS__)
#define SK_ERROR(...) ::sk::log(::sk::LogLevel::Error, __VA_ARGS__)
