#pragma once

#include <algorithm>
#include <cstddef>
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

    // An App with no window and a recording logger. Run(n) runs exactly n frames of FrameDelta seconds each through
    // App::RunFrame, then the Exit phase.
    class HeadlessApp
    {
      public:
        // One tick per frame, whatever the wall clock does.
        static constexpr double FrameDelta = 1.0 / 60.0;

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
            for (int frame = 0; frame < frames; ++frame)
            {
                m_app.RunFrame(FrameDelta);
                ++m_frames;
            }
            m_app.RunExit();
        }

      private:
        RecordingLogger* m_logger; // Owned by the App.
        App m_app;
        int m_frames = 0;
    };

    // The events of type TEvent the app's queue still holds, oldest first: what a reader made now would read.
    template <typename TEvent>
    std::vector<TEvent> HeldEvents(const App& app)
    {
        EventReader<TEvent> reader;
        const EventRange<TEvent> events = reader.Read(app);
        return {events.begin(), events.end()};
    }

    // How many events a queue still holds: what a reader made now would read, by name and as bytes.
    inline std::size_t HeldEventCount(const IEventQueue& queue)
    {
        std::size_t cursor = NewReaderCursor;
        return queue.ReadRawAfter(cursor).Count();
    }
} // namespace Fenrir::Testing
