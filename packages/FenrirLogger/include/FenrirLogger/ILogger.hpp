#pragma once

#include "spdlog/fmt/fmt.h"

#include <string>

namespace Fenrir
{
    class ILogger
    {
      public:
        virtual ~ILogger() = default;

        template <typename... Args>
        void Log(const std::string& format, Args&&... args);

        template <typename... Args>
        void Info(const std::string& format, Args&&... args);

        template <typename... Args>
        void Warn(const std::string& format, Args&&... args);

        template <typename... Args>
        void Error(const std::string& format, Args&&... args);

        template <typename... Args>
        void Fatal(const std::string& format, Args&&... args);

      protected:
        virtual void LogImpl(const std::string& message) = 0;
        virtual void InfoImpl(const std::string& message) = 0;
        virtual void WarnImpl(const std::string& message) = 0;
        virtual void ErrorImpl(const std::string& message) = 0;
        virtual void FatalImpl(const std::string& message) = 0;
    };

    template <typename... Args>
    void ILogger::Log(const std::string& format, Args&&... args)
    {
        LogImpl(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void ILogger::Info(const std::string& format, Args&&... args)
    {
        InfoImpl(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void ILogger::Warn(const std::string& format, Args&&... args)
    {
        WarnImpl(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void ILogger::Error(const std::string& format, Args&&... args)
    {
        ErrorImpl(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void ILogger::Fatal(const std::string& format, Args&&... args)
    {
        FatalImpl(fmt::format(format, std::forward<Args>(args)...));
    }
} // namespace Fenrir