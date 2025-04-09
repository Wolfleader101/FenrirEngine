#pragma once

#include <format>
#include <string_view>

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
        void Log(std::string_view format, Args&&... args);

        template <typename... Args>
        void Info(std::string_view format, Args&&... args);

        template <typename... Args>
        void Warn(std::string_view format, Args&&... args);

        template <typename... Args>
        void Error(std::string_view format, Args&&... args);

        template <typename... Args>
        void Fatal(std::string_view format, Args&&... args);

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
        virtual void LogImpl(std::string_view message) = 0;
        virtual void InfoImpl(std::string_view message) = 0;
        virtual void WarnImpl(std::string_view message) = 0;
        virtual void ErrorImpl(std::string_view message) = 0;
        virtual void FatalImpl(std::string_view message) = 0;
    };

    template <typename... Args>
    void ILogger::Log(std::string_view format, Args&&... args)
    {
        LogImpl(std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void ILogger::Info(std::string_view format, Args&&... args)
    {
        InfoImpl(std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void ILogger::Warn(std::string_view format, Args&&... args)
    {
        WarnImpl(std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void ILogger::Error(std::string_view format, Args&&... args)
    {
        ErrorImpl(std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void ILogger::Fatal(std::string_view format, Args&&... args)
    {
        FatalImpl(std::vformat(format, std::make_format_args(args...)));
    }
} // namespace Fenrir
