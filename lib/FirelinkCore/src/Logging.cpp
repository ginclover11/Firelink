#include <FirelinkCore/Logging.h>
#include <FirelinkCore/MemoryUtils.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
    std::filesystem::path LOG_FILE_PATH;
    std::ofstream LOG_FILE;
    std::mutex LOG_MUTEX;

    auto LOG_LEVEL = Firelink::LogLevel::INFO;

    // Helper function to get the current date and time as a std::wstring
    std::string GetCurrentDateTime()
    {
        // Get current time
        const auto now = std::chrono::system_clock::now();
        const auto inTimeT = std::chrono::system_clock::to_time_t(now);

        // Create a tm structure to store local time
        tm timeInfo{};

#ifdef _WIN32
        // Use localtime_s instead of localtime to avoid C4996 error
        if (const auto result = localtime_s(&timeInfo, &inTimeT); result != 0)
        {
            std::cerr << "Error getting current logging time: " << result << '\n';
            return "";
        }
#else
        // POSIX equivalent (thread-safe, unlike `localtime`).
        if (localtime_r(&inTimeT, &timeInfo) == nullptr)
        {
            std::cerr << "Error getting current logging time: " << errno << '\n';
            return "";
        }
#endif

        // Format the time into a std::string
        std::stringstream ss;
        ss << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    // Log a message with the given level.
    void Log(const std::string& level, const std::string& message, const bool toStdErr = false)
    {
        std::lock_guard lock(LOG_MUTEX);

        const std::string logEntry = std::format("[{}] {} {}", GetCurrentDateTime(), level, message);

        if (LOG_FILE.is_open())
        {
            LOG_FILE << logEntry << '\n';
            LOG_FILE.flush();
        }
        else
        {
            (toStdErr ? std::cerr : std::cout) << logEntry << '\n';
        }
    }
} // namespace

// Enable logging to a file
void Firelink::SetLogFile(const std::filesystem::path& filePath)
{
    std::unique_lock lock(LOG_MUTEX);

    if (LOG_FILE.is_open())
    {
        if (LOG_FILE_PATH == filePath)
            return; // same file already set as log file
        // Close previous log file so we can open the new one below.
        LOG_FILE.close();
    }

    LOG_FILE_PATH = filePath;

    // Open the log file (new file or overwrite existing)
    LOG_FILE.open(LOG_FILE_PATH.c_str(), std::ios::out);
    if (!LOG_FILE.is_open())
    {
        std::cerr << format("Failed to open log file path: {}", LOG_FILE_PATH.string()) << '\n';
    }

    // Release std::mutex.
    lock.unlock();
    Debug(std::format("Log file opened: {}", LOG_FILE_PATH.string()));
}

// Disable logging to a file
void Firelink::ClearLogFile()
{
    std::lock_guard lock(LOG_MUTEX);
    if (LOG_FILE.is_open())
    {
        Info(std::format("Log file closing: {}", LOG_FILE_PATH.string()));
        LOG_FILE.close();
        Info("Log file closed.");
    }
}

void Firelink::SetLogLevel(const LogLevel& level)
{
    LOG_LEVEL = level;
}

void Firelink::Debug(const std::string& message)
{
    if (LOG_LEVEL > LogLevel::DEBUG)
        return;
    Log("  [DEBUG] ", message);
}

void Firelink::Info(const std::string& message)
{
    if (LOG_LEVEL > LogLevel::INFO)
        return;
    Log("   [INFO] ", message);
}

void Firelink::Warning(const std::string& message)
{
    if (LOG_LEVEL > LogLevel::WARNING)
        return;
    Log("[WARNING] ", message);
}

void Firelink::Error(const std::string& message)
{
    if (LOG_LEVEL > LogLevel::ERR)
        return;
    Log("  [ERROR] ", message, true);
}

void Firelink::WinError(const std::string& message)
{
    if (LOG_LEVEL > LogLevel::ERR)
        return;
#ifdef _WIN32
    const DWORD errorCode = GetLastError();
    LPSTR errorBuffer = nullptr;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&errorBuffer),
        0,
        nullptr);
    const std::string errorMessage = std::format("{} (WinError {}): {}", message, std::to_string(errorCode), errorBuffer);
    LocalFree(errorBuffer);
    Error(errorMessage);
#else
    // No Win32 last-error on this platform; report `errno` instead.
    const int errorCode = errno;
    Error(std::format("{} (errno {}): {}", message, errorCode, std::strerror(errorCode)));
#endif
}
