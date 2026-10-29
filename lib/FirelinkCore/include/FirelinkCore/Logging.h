#pragma once

#include <FirelinkCore/Export.h>

#include <filesystem>
#include <format>
#include <string>
#include <utility>

namespace Firelink
{
    enum class LogLevel
    {
        DEBUG,
        INFO,
        WARNING,
        ERR,
        NONE,
    };

    // While file logging is set, output will go to `filePath` instead of stdout or stderr.
    FIRELINK_CORE_API void SetLogFile(const std::filesystem::path& filePath);
    FIRELINK_CORE_API void ClearLogFile();

    FIRELINK_CORE_API void SetLogLevel(const LogLevel& level);

    FIRELINK_CORE_API void Debug(const std::string& message);
    FIRELINK_CORE_API void Info(const std::string& message);
    FIRELINK_CORE_API void Warning(const std::string& message);
    FIRELINK_CORE_API void Error(const std::string& message);
    // Calls `GetLastError()` (Windows) or reads `errno` (other platforms) and appends it automatically.
    FIRELINK_CORE_API void WinError(const std::string& message);

    // Template versions with variadic `std::format` args.

    template<typename... Args>
    void Debug(const std::string& format, Args&&... args)
    {
        Debug(std::vformat(format, std::make_format_args(args...)));
    }
    template<typename... Args>
    void Info(const std::string& format, Args&&... args)
    {
        Info(std::vformat(format, std::make_format_args(args...)));
    }
    template<typename... Args>
    void Warning(const std::string& format, Args&&... args)
    {
        Warning(std::vformat(format, std::make_format_args(args...)));
    }
    template<typename... Args>
    void Error(const std::string& format, Args&&... args)
    {
        Error(std::vformat(format, std::make_format_args(args...)));
    }
} // namespace Firelink
