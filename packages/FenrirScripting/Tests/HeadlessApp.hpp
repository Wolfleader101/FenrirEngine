#pragma once

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"

namespace Fenrir::Testing
{
    enum class LogLevel
    {
        Log,
        Info,
        Warn,
        Error,
        Fatal
    };

    struct LoggedMessage
    {
        LogLevel level;
        std::string text;
    };

    // A logger that remembers what it was given, so tests can look at it.
    class RecordingLogger : public ILogger
    {
      public:
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

        [[nodiscard]] std::vector<LoggedMessage> Messages() const
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            return m_messages;
        }

        [[nodiscard]] bool Contains(LogLevel level, const std::string& text) const
        {
            const auto messages = Messages();
            return std::any_of(messages.begin(), messages.end(), [&](const LoggedMessage& message) {
                return message.level == level && message.text.find(text) != std::string::npos;
            });
        }

      protected:
        void LogImpl(std::string_view message) override
        {
            Record(LogLevel::Log, message);
        }
        void InfoImpl(std::string_view message) override
        {
            Record(LogLevel::Info, message);
        }
        void WarnImpl(std::string_view message) override
        {
            Record(LogLevel::Warn, message);
        }
        void ErrorImpl(std::string_view message) override
        {
            Record(LogLevel::Error, message);
        }
        void FatalImpl(std::string_view message) override
        {
            Record(LogLevel::Fatal, message);
        }

      private:
        void Record(LogLevel level, std::string_view message)
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_messages.push_back({level, std::string(message)});
            // FENRIR_TEST_ECHO=1 shows what the app logged as it happens, for finding out why a case failed.
            static const bool echo = std::getenv("FENRIR_TEST_ECHO") != nullptr;
            if (echo)
            {
                std::cerr << "[log] " << message << std::endl;
            }
        }

        mutable std::mutex m_mutex;
        std::vector<LoggedMessage> m_messages;
    };

    // An App with no window and a recording logger. Run(n) runs exactly n frames, then stops.
    class HeadlessApp
    {
      public:
        HeadlessApp() : m_logger(new RecordingLogger), m_app(std::unique_ptr<ILogger>(m_logger))
        {
        }

        App& Get()
        {
            return m_app;
        }
        [[nodiscard]] const RecordingLogger& Logger() const
        {
            return *m_logger;
        }
        [[nodiscard]] int FramesRun() const
        {
            return m_frames;
        }

        void Run(int frames)
        {
            m_app.AddSequentialSystems(SchedulePriority::Update, {[this, frames](App& app) {
                                           if (++m_frames >= frames)
                                           {
                                               app.Stop();
                                           }
                                       }});
            m_app.Run();
        }

      private:
        RecordingLogger* m_logger; // Owned by the App.
        App m_app;
        int m_frames = 0;
    };
} // namespace Fenrir::Testing
