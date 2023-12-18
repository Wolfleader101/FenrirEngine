#include "FenrirLogger/ConsoleLogger.hpp"

#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Fenrir
{
    ConsoleLogger::ConsoleLogger() : m_logger()
    {
        m_logger = spdlog::stdout_color_mt("FENRIR");
        m_logger->set_pattern("%^[%T] %n: %v%$");
        m_logger->set_level(spdlog::level::trace);
    }

    void ConsoleLogger::LogImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::trace, message);
    }

    void ConsoleLogger::InfoImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::info, message);
    }

    void ConsoleLogger::WarnImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::warn, message);
    }

    void ConsoleLogger::ErrorImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::err, message);
    }

    void ConsoleLogger::FatalImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::critical, message);
    }
} // namespace Fenrir