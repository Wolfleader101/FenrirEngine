#pragma once

#include <vector>

#include <spdlog/spdlog.h>

#include "FenrirLogger/ILogger.hpp"

namespace Fenrir
{

    /**
     * @brief in-built console logger class that uses spdlog under the hood
     *
     */
    class ConsoleLogger : public ILogger
    {
      public:
        ConsoleLogger();

        /**
         * @brief Add a logger to the console logger, this are used to log to multiple places, and equivalent to sinks
         *
         * @param logger the logger to add
         */
        void AddLogger(std::shared_ptr<ILogger> logger);

        /**
         * @brief Remove a logger from the console logger
         *
         * @param logger the logger to remove
         */
        void RemoveLogger(std::shared_ptr<ILogger> logger);

      protected:
        void LogImpl(const std::string& message) override;
        void InfoImpl(const std::string& message) override;
        void WarnImpl(const std::string& message) override;
        void ErrorImpl(const std::string& message) override;
        void FatalImpl(const std::string& message) override;

      private:
        std::shared_ptr<spdlog::logger> m_logger;
        std::vector<std::shared_ptr<ILogger>> m_loggers;
    };
} // namespace Fenrir