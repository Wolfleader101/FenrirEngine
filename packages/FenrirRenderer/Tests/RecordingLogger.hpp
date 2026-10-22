#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirLogger/ILogger.hpp"

namespace RendererTest
{
    /** @brief What a RecordingLogger was told, by level. */
    struct LoggedMessages
    {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    /**
     * @brief A logger that keeps the errors and warnings it is given, for tests that check a diagnostic, and drops the
     * rest. Fatal messages count as errors.
     */
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<LoggedMessages> messages) : m_messages(std::move(messages))
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
        void WarnImpl(std::string_view message) override
        {
            m_messages->warnings.emplace_back(message);
        }
        void ErrorImpl(std::string_view message) override
        {
            m_messages->errors.emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_messages->errors.emplace_back(message);
        }

      private:
        std::shared_ptr<LoggedMessages> m_messages;
    };
} // namespace RendererTest
