#pragma once

#include <spdlog/spdlog.h>

#include "FenrirLogger/ILogger.hpp"

namespace Fenrir
{
    class ConsoleLogger : public ILogger
    {
      public:
        ConsoleLogger();

      protected:
        void LogImpl(const std::string& message) override;
        void InfoImpl(const std::string& message) override;
        void WarnImpl(const std::string& message) override;
        void ErrorImpl(const std::string& message) override;
        void FatalImpl(const std::string& message) override;

      private:
        std::shared_ptr<spdlog::logger> m_logger;
    };
} // namespace Fenrir