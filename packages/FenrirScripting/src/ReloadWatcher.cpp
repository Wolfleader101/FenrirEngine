#include "ReloadWatcher.hpp"

namespace Fenrir::Detail
{
    ReloadWatcher::Watch ReloadWatcher::Poll(Clock::time_point now,
                                             const std::function<std::optional<Stamp>()>& lookAtFile,
                                             const std::function<bool()>& canBeOpened)
    {
        if (!m_settings.hotReload || now - m_lastLook < m_settings.interval)
        {
            return Watch::Idle;
        }
        m_lastLook = now;
        const auto stamp = lookAtFile();
        if (!stamp.has_value() || stamp == m_loaded)
        {
            m_pending.reset();
            return Watch::Idle;
        }
        // A build is still going if the file changes between looks, so only a stable one is loaded.
        if (!m_pending.has_value() || !(*m_pending == *stamp))
        {
            m_pending = stamp;
            m_pendingSince = now;
            return Watch::Settling;
        }
        if (now - m_pendingSince < m_settings.debounce || !canBeOpened())
        {
            return Watch::Settling;
        }
        return Watch::Ready;
    }
} // namespace Fenrir::Detail
