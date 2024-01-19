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

        void AddLogger(std::shared_ptr<ILogger> logger) override;

        void RemoveLogger(std::shared_ptr<ILogger> logger) override;

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