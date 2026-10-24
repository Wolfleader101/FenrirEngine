#include "LogSink.hpp"

#include "ManagedDiagnosticBuffer.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define IRIS_CALL __cdecl
#else
#define IRIS_CALL
#endif

namespace Iris::Detail
{
    namespace
    {
        // Deliberately leaked: managed code may log while owners are destroyed late, after static
        // destruction would otherwise have freed this.
        struct SinkState
        {
            std::mutex mutex;
            std::shared_ptr<const LogSink> sink;
        };

        SinkState& State()
        {
            static SinkState* const state = new SinkState;
            return *state;
        }

        // Called by the bridge. A sink failure must never reach managed code, and a sink that clears or
        // replaces itself keeps running safely because this holds its own reference.
        void IRIS_CALL Trampoline(void*, std::int32_t level, const std::uint8_t* text, std::int32_t length) noexcept
        {
            try
            {
                std::shared_ptr<const LogSink> sink;
                {
                    const std::lock_guard lock(State().mutex);
                    sink = State().sink;
                }
                if (sink == nullptr)
                {
                    return;
                }
                const LogMessage message{static_cast<LogLevel>(level),
                                         std::string_view(reinterpret_cast<const char*>(text),
                                                          static_cast<std::size_t>(length < 0 ? 0 : length))};
                (*sink)(message);
            }
            catch (...)
            {
            }
        }
    } // namespace

    void EmitLog(LogLevel level, std::string_view text) noexcept
    {
        try
        {
            std::shared_ptr<const LogSink> sink;
            {
                const std::lock_guard lock(State().mutex);
                sink = State().sink;
            }
            if (sink != nullptr)
            {
                (*sink)(LogMessage{level, text});
            }
        }
        catch (...)
        {
        }
    }

    CallResult<void> InstallLogSink(const BridgeFunctions& bridge, LogSink sink)
    {
        const bool enable = static_cast<bool>(sink);
        {
            const std::lock_guard lock(State().mutex);
            State().sink = enable ? std::make_shared<const LogSink>(std::move(sink)) : nullptr;
        }
        ManagedDiagnosticBuffer diagnostic;
        const auto status = bridge.setLogSink(enable ? &Trampoline : nullptr, nullptr, diagnostic.bytes.data(),
                                              diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        return CompleteCall<void>(status, "SetLogSink", diagnostic);
    }
} // namespace Iris::Detail
