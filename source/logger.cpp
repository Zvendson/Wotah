/**
 * @file logger.cpp
 * The log on top of spdlog: one asynchronous logger whose single writer thread feeds a colored console sink and a
 * file sink next to the executable.
 */
#include "logger.hpp"

#include "utils.hpp"

#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    /** "[16:20:00][info] message": %H:%M:%S is the 24-hour local time, %^...%$ colors the level name %l on the
     *  console (the file gets no color codes), %v is the message. Flags: spdlog wiki, "Custom formatting". */
    constexpr const char* LinePattern = "[%H:%M:%S][%^%l%$] %v";

    /** The log file's name; it is written next to the executable. */
    constexpr const char* LogFileName = "wotah.log";

    /** The logger's name inside spdlog; it is never registered, so it only shows up in spdlog's own error messages. */
    constexpr const char* LoggerName = "wotah";

    /** How many lines may wait for the writer thread. When the queue is full the oldest waiting line is dropped, so a
     *  burst of lines never makes the program wait. */
    constexpr std::size_t QueueSize = 8192;

    /** One writer thread, so lines reach the console and the file in the order they were logged. */
    constexpr std::size_t WriterThreadCount = 1;

    /** The writer thread and its queue. The logger only holds a weak pointer to it, so it is kept here. */
    std::shared_ptr<spdlog::details::thread_pool> s_writer;

    /** The logger every line goes through; empty before Create and after Destroy. */
    std::shared_ptr<spdlog::async_logger> s_logger;

    /** The spdlog level for one of ours. */
    spdlog::level::level_enum ToSpdlogLevel(Wotah::LogLevel level)
    {
        switch (level)
        {
            case Wotah::LogLevel::Trace:
                return spdlog::level::trace;
            case Wotah::LogLevel::Debug:
                return spdlog::level::debug;
            case Wotah::LogLevel::Info:
                return spdlog::level::info;
            case Wotah::LogLevel::Warning:
                return spdlog::level::warn;
            case Wotah::LogLevel::Error:
                return spdlog::level::err;
            case Wotah::LogLevel::Critical:
                return spdlog::level::critical;
        }
        return spdlog::level::critical;
    }
} // namespace

void Wotah::Logger::Create()
{
    if (s_logger)
    {
        return;
    }

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    // spdlog reports a file it cannot open with an exception; it is caught here so the console still gets a logger
    // and the reason is logged once that logger exists.
    const std::optional<std::filesystem::path> folder = FindExecutableDirectory();
    const std::filesystem::path filePath = folder ? *folder / LogFileName : std::filesystem::path(LogFileName);
    std::string fileError;
    try
    {
        constexpr bool Truncate = true; // every start begins a fresh file
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(filePath.native(), Truncate));
    }
    catch (const spdlog::spdlog_ex& exception)
    {
        fileError = exception.what();
    }

    s_writer = std::make_shared<spdlog::details::thread_pool>(QueueSize, WriterThreadCount);
    s_logger = std::make_shared<spdlog::async_logger>(LoggerName, sinks.begin(), sinks.end(), s_writer,
                                                      spdlog::async_overflow_policy::overrun_oldest);
    s_logger->set_pattern(LinePattern);
    s_logger->set_level(spdlog::level::trace); // the header's MinimumLogLevel already filtered
    s_logger->flush_on(spdlog::level::warn);   // warnings and errors reach the file right away

    if (!folder)
    {
        Warning("Could not find the executable's folder, so {} is in the working folder", LogFileName);
    }
    if (!fileError.empty())
    {
        Error("Could not open the log file, so lines only go to the console: {}", fileError);
    }
}

void Wotah::Logger::Destroy()
{
    if (!s_logger)
    {
        return;
    }

    // Lines dropped because the queue was full are reported, so a gap in the log has a visible reason.
    if (const std::size_t dropped = s_writer->overrun_counter(); dropped > 0)
    {
        Warning("{} log lines were dropped because they came faster than they could be written", dropped);
    }

    // Order matters: the waiting lines hold the logger, and the writer thread only stops after writing all of them,
    // so the logger is dropped first and the writer last; destroying the writer joins its thread.
    s_logger.reset();
    s_writer.reset();
}

void Wotah::Logger::Write(LogLevel level, std::string_view message)
{
    if (!s_logger)
    {
        std::fwrite(message.data(), 1, message.size(), stderr);
        std::fputc('\n', stderr);
        return;
    }
    s_logger->log(ToSpdlogLevel(level), spdlog::string_view_t(message.data(), message.size()));
}
