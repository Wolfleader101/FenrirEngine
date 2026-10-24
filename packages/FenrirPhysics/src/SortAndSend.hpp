#pragma once

#include <algorithm>
#include <vector>

#include "FenrirApp/App.hpp"

namespace Fenrir::Detail
{
    /** @brief Sends `events` to `app` in the order of `key`, so the order is the same on every run whatever order the
     * backend found them in. */
    template <typename TEvent, typename Key>
    void SortAndSend(App& app, std::vector<TEvent>& events, Key key)
    {
        std::sort(events.begin(), events.end(), [&key](const TEvent& a, const TEvent& b) { return key(a) < key(b); });
        for (const TEvent& event : events)
        {
            app.SendEvent(event);
        }
    }
} // namespace Fenrir::Detail
