#pragma once

#include <string_view>

namespace hiksadp {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

class Logger {
public:
    static void set_log_file(std::string_view path);
    static void write(LogLevel level, std::string_view message);
};

} // namespace hiksadp

