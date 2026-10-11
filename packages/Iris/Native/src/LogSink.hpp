#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/CallResult.hpp"
#include "Iris/Log.hpp"

#include <string_view>

namespace Iris::Detail
{
    // Installs (or, for an empty sink, clears) the process-wide log sink through the given bridge. The
    // sink state is never destroyed, so the bridge can call back at any time before the process exits.
    CallResult<void> InstallLogSink(const BridgeFunctions& bridge, LogSink sink);

    // Delivers a message from native code to the current sink, if any. Never throws.
    void EmitLog(LogLevel level, std::string_view text) noexcept;
} // namespace Iris::Detail
