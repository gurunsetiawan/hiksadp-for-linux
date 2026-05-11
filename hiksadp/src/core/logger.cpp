#include "core/logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace hiksadp {
namespace {

std::mutex& log_mutex()
{
    static std::mutex m;
    return m;
}

std::string& log_file_path()
{
    static std::string p{"logs/hiksadp.log"};
    return p;
}

const char* level_to_cstr(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

std::string now_string()
{
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // namespace

void Logger::set_log_file(std::string_view path)
{
    std::lock_guard lock{log_mutex()};
    log_file_path() = std::string{path};
}

void Logger::write(LogLevel level, std::string_view message)
{
    std::lock_guard lock{log_mutex()};
    const std::filesystem::path p{log_file_path()};
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream out(p, std::ios::app);
    if (!out.is_open()) return;
    out << "[" << now_string() << "]"
        << "[" << level_to_cstr(level) << "] "
        << message << "\n";
}

} // namespace hiksadp

