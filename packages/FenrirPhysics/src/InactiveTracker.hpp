#pragma once

// The glossary's inactive rule, for one kind of component: an inactive component is kept, nothing is made for it, one
// warning is logged when it becomes inactive, and it comes back by itself once valid. What else its owner does for it
// (dropping a constraint, resetting a state) stays the owner's. Private to the FenrirPhysics package.

#include <cstdint>
#include <set>
#include <string>

#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Detail
{
    class InactiveTracker
    {
      public:
        /** @brief The warning for the component on `entity` becoming inactive, from `why`, the reason it is. */
        using Message = std::string (*)(std::uint32_t entity, const std::string& why);

        /** @brief Tracks one kind of component, warning through `logger` with `message`. */
        InactiveTracker(ILogger& logger, Message message) : m_logger(&logger), m_message(message)
        {
        }

        /**
         * @brief Records why the component on `entity` is inactive now, `why` empty if it is valid. Warns when it
         * becomes inactive (from valid, or from not yet known); a different reason while still inactive is no new
         * warning. Returns whether it is inactive.
         */
        bool Report(std::uint32_t entity, const std::string& why)
        {
            if (why.empty())
            {
                m_inactive.erase(entity);
                return false;
            }
            if (m_inactive.insert(entity).second)
            {
                m_logger->Warn(m_message(entity, why));
            }
            return true;
        }

        /** @brief Forgets `entity`, whose component went (or went and came back): it is warned about afresh. */
        void Forget(std::uint32_t entity)
        {
            m_inactive.erase(entity);
        }

        /** @brief Forgets each entity not in `live`, the entities that still have the component. */
        void Keep(const std::set<std::uint32_t>& live)
        {
            std::erase_if(m_inactive, [&live](std::uint32_t entity) { return !live.contains(entity); });
        }

      private:
        ILogger* m_logger;
        Message m_message;
        // The entities whose component is inactive now, each warned about when it became so.
        std::set<std::uint32_t> m_inactive;
    };
} // namespace Fenrir::Detail
