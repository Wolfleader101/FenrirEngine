#pragma once

#include <format>
#include <string>

namespace Fenrir
{

    /**
     * @brief Interface for logging
     *
     */
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

        /**
         * @brief Add a logger to the console logger, this are used to log to multiple places, and equivalent to sinks
         *
         * @param logger the logger to add
         */
        virtual void AddLogger(std::shared_ptr<ILogger> logger) = 0;

        /**
         * @brief Remove a logger from the console logger
         *
         * @param logger the logger to remove
         */
        virtual void RemoveLogger(std::shared_ptr<ILogger> logger) = 0;

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
        LogImpl(std::vformat(format, std::make_format_args(std::forward<Args>(args)...)));
    }

    template <typename... Args>
    void ILogger::Info(const std::string& format, Args&&... args)
    {
        InfoImpl(std::vformat(format, std::make_format_args(std::forward<Args>(args)...)));
    }

    template <typename... Args>
    void ILogger::Warn(const std::string& format, Args&&... args)
    {
        WarnImpl(std::vformat(format, std::make_format_args(std::forward<Args>(args)...)));
    }

    template <typename... Args>
    void ILogger::Error(const std::string& format, Args&&... args)
    {
        ErrorImpl(std::vformat(format, std::make_format_args(std::forward<Args>(args)...)));
    }

    template <typename... Args>
    void ILogger::Fatal(const std::string& format, Args&&... args)
    {
        FatalImpl(std::vformat(format, std::make_format_args(std::forward<Args>(args)...)));
    }
} // namespace Fenrir