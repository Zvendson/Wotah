/**
 * @file logger.hpp
 * The program's log: colored lines on the console and the same lines in wotah.log next to the executable.
 * A background thread does the writing, so logging never waits for the console or the disk.
 *
 *     Wotah::Logger::Create();
 *     Wotah::Logger::Info("Using {} at {}x{}", gpuName, width, height); // [16:20:00][info] Using ...
 *     Wotah::Logger::Destroy();
 *
 * spdlog does the work but stays inside logger.cpp; this header only needs <format>.
 */
#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace Wotah
{
    /** How important a log line is, from least to most. */
    enum class LogLevel
    {
        Trace,
        Debug,
        Info,
        Warning,
        Error,
        Critical,
    };

    /** Lines below this level are skipped before they are even formatted: everything in debug builds, Info and up in
     *  release builds. */
#ifdef NDEBUG
    inline constexpr LogLevel MinimumLogLevel = LogLevel::Info;
#else
    inline constexpr LogLevel MinimumLogLevel = LogLevel::Trace;
#endif

    /**
     * The program-wide log. Lines look like "[16:20:00][info] message": 24-hour local time, the level in its color,
     * the message. Create it before logging and destroy it at the end; lines written outside that go to stderr.
     * Create and Destroy run on the main thread, while no other thread logs; the log functions are thread-safe.
     */
    class Logger final
    {
    public:
        Logger() = delete;

        /** Starts the writer thread and opens the console and the log file (overwritten on every start). A file that
         *  cannot be opened is logged as an error; the console keeps working. */
        static void Create();

        /** Logs how many lines were dropped (if any), writes every waiting line, closes the log file and stops the
         *  writer thread; safe to call more than once. */
        static void Destroy();

        /** Very detailed lines for following the program step by step. */
        template <typename... Args>
        static void Trace(std::format_string<Args...> format, Args&&... args)
        {
            Log(LogLevel::Trace, format, std::forward<Args>(args)...);
        }

        /** Lines that help while developing. */
        template <typename... Args>
        static void Debug(std::format_string<Args...> format, Args&&... args)
        {
            Log(LogLevel::Debug, format, std::forward<Args>(args)...);
        }

        /** What the program is doing. */
        template <typename... Args>
        static void Info(std::format_string<Args...> format, Args&&... args)
        {
            Log(LogLevel::Info, format, std::forward<Args>(args)...);
        }

        /** Something unexpected that the program handles. */
        template <typename... Args>
        static void Warning(std::format_string<Args...> format, Args&&... args)
        {
            Log(LogLevel::Warning, format, std::forward<Args>(args)...);
        }

        /** Something failed. */
        template <typename... Args>
        static void Error(std::format_string<Args...> format, Args&&... args)
        {
            Log(LogLevel::Error, format, std::forward<Args>(args)...);
        }

        /** Something failed so badly that the program cannot go on. */
        template <typename... Args>
        static void Critical(std::format_string<Args...> format, Args&&... args)
        {
            Log(LogLevel::Critical, format, std::forward<Args>(args)...);
        }

    private:
        /** Formats a line on the caller's thread and hands it to the writer thread. */
        template <typename... Args>
        static void Log(LogLevel level, std::format_string<Args...> format, Args&&... args)
        {
            if (level < MinimumLogLevel)
            {
                return;
            }
            // shortcut: std::format allocates a string per line, fine while nothing logs per frame; format into a
            // reused buffer once something does.
            Write(level, std::format(format, std::forward<Args>(args)...));
        }

        /** Queues a finished line for the writer thread, or prints it to stderr when there is no log. */
        static void Write(LogLevel level, std::string_view message);
    };
} // namespace Wotah
