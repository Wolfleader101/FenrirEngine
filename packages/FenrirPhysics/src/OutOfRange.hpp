#pragma once

// Correcting a component's out-of-range values as physics applies it, warning about each kind of problem it holds, on
// WarnOnce. Private to the FenrirPhysics package.

#include <algorithm>
#include <cstdint>
#include <string>

#include "FenrirLogger/ILogger.hpp"
#include "WarnOnce.hpp"

namespace Fenrir::Detail
{
    class OutOfRange
    {
      public:
        /** @brief How long a kind of problem stays warned about. */
        enum class Lifetime
        {
            // While the component lives: its owner keeps `warned` with the component, and forgets it with it.
            PerComponent,
            // Until the component is next corrected without it: a later out-of-range value warns again.
            UntilValid,
        };

        /**
         * @brief Corrects the `component` on `entity` (its type's name, "Joint"), warning through `logger`. `warned` is
         * the bitmask of the kinds of problem it has been warned about, kept by its owner between corrections; with
         * UntilValid it becomes the kinds this correction finds.
         */
        OutOfRange(const char* component, std::uint32_t entity, std::uint32_t& warned, ILogger& logger,
                   Lifetime lifetime)
            : m_component(component), m_entity(entity), m_warned(&warned), m_logger(&logger)
        {
            if (lifetime == Lifetime::UntilValid)
            {
                m_before = warned;
                warned = 0;
            }
        }

        /** @brief The value held a kind of problem, `problem`: warns with `message`, if it is not already warned
         * about. */
        void Warn(std::uint32_t problem, const char* message)
        {
            if ((m_before & problem) != 0)
            {
                // Still held since the last correction: warned about then.
                *m_warned |= problem;
                return;
            }
            WarnOnce(*m_warned, problem, *m_logger,
                     "The " + std::string(m_component) + " on entity " + std::to_string(m_entity) + ": " + message);
        }

        /** @brief Raises a negative `value` to 0, warning with `message`. */
        void NotNegative(float& value, std::uint32_t problem, const char* message)
        {
            if (value < 0.0f)
            {
                value = 0.0f;
                Warn(problem, message);
            }
        }

        /** @brief Moves `value` into [low, high], warning with `message` if it was outside. */
        void Within(float& value, float low, float high, std::uint32_t problem, const char* message)
        {
            if (value < low || value > high)
            {
                value = std::clamp(value, low, high);
                Warn(problem, message);
            }
        }

      private:
        const char* m_component;
        std::uint32_t m_entity;
        std::uint32_t* m_warned;
        ILogger* m_logger;
        // With UntilValid, the kinds of problem the last correction found, so still holding one is no new warning.
        std::uint32_t m_before = 0;
    };
} // namespace Fenrir::Detail
