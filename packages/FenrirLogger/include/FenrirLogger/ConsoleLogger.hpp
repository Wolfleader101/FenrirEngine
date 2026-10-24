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
        void LogImpl(std::string_view message) override;
        void InfoImpl(std::string_view message) override;
        void WarnImpl(std::string_view message) override;
        void ErrorImpl(std::string_view message) override;
        void FatalImpl(std::string_view message) override;

      private:
        std::shared_ptr<spdlog::logger> m_logger;
        std::vector<std::shared_ptr<ILogger>> m_loggers;
    };
} // namespace Fenrir