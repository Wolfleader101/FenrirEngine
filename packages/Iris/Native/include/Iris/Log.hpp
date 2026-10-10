#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace Iris
{
    // Wire values, mirrored by the managed LogLevel.
    enum class LogLevel : std::int32_t
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3
    };

    /**
     * @brief One message from the bridge.
     *
     * `text` is UTF-8 and is only valid during the sink call; copy it to keep it. Bridge events (a
     * component loaded or released, an assembly shared) are Info. A managed exception caught at a bridge
     * boundary is reported with its full ToString(): Error for exceptions from component code, Warning for
     * the bridge's own argument validation. A lookup that finds nothing is not an exception and is not
     * logged. The call that raised an exception still returns the same details in its own result.
     */
    struct LogMessage
    {
        LogLevel level = LogLevel::Info;
        std::string_view text;
    };

    /**
     * @brief Receives bridge messages. Registered with Host::SetLogSink.
     *
     * The sink runs synchronously on the thread making the bridge call, so it needs no synchronization of
     * its own beyond what that thread's calls already require. It may throw (the exception is swallowed)
     * and may clear or replace the sink, but must not call into Iris for anything else.
     */
    using LogSink = std::function<void(const LogMessage&)>;
} // namespace Iris
