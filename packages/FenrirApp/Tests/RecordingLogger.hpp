#pragma once

// A logger for FenrirApp's type registry tests that keeps the errors it is given, so a test can say whether the
// registry reported anything: a second ConsoleLogger in one process throws.

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirLogger/ILogger.hpp"

namespace AppTest
{
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<std::vector<std::string>> errors) : m_errors(std::move(errors))
        {
        }

        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view) override
        {
        }
        void ErrorImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_errors;
    };

    /** @brief How many of `errors` the type registry reported: each of its refusals starts "cannot register". */
    inline std::size_t RegistryErrors(const std::vector<std::string>& errors)
    {
        std::size_t count = 0;
        for (const std::string& error : errors)
            if (error.find("cannot register") != std::string::npos)
                ++count;
        return count;
    }
} // namespace AppTest
