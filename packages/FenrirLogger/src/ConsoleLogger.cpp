#include "FenrirLogger/ConsoleLogger.hpp"

#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Fenrir
{
    ConsoleLogger::ConsoleLogger() : m_logger(), m_loggers()
    {
        m_logger = spdlog::stdout_color_mt("FENRIR");
        m_logger->set_pattern("%^[%T] %v%$");
        m_logger->set_level(spdlog::level::trace);
    }

    void ConsoleLogger::LogImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::trace, message);

        for (auto& logger : m_loggers)
        {
            logger->Log(message);
        }
    }

    void ConsoleLogger::InfoImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::info, message);

        for (auto& logger : m_loggers)
        {
            logger->Info(message);
        }
    }

    void ConsoleLogger::WarnImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::warn, message);

        for (auto& logger : m_loggers)
        {
            logger->Warn(message);
        }
    }

    void ConsoleLogger::ErrorImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::err, message);

        for (auto& logger : m_loggers)
        {
            logger->Error(message);
        }
    }

    void ConsoleLogger::FatalImpl(const std::string& message)
    {
        m_logger->log(spdlog::level::critical, message);

        for (auto& logger : m_loggers)
        {
            logger->Fatal(message);
        }
    }

    void ConsoleLogger::AddLogger(std::shared_ptr<ILogger> logger)
    {
        m_loggers.push_back(logger);
    }

    void ConsoleLogger::RemoveLogger(std::shared_ptr<ILogger> logger)
    {
        m_loggers.erase(std::remove(m_loggers.begin(), m_loggers.end(), logger), m_loggers.end());
    }

} // namespace Fenrir