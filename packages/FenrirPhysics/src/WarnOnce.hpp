#pragma once

// Warning once about each kind of problem a component holds. Private to the FenrirPhysics package.

#include <cstdint>
#include <string>

#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief Logs `message` if `warned`, the bitmask of the kinds of problem already warned about, lacks `problem`,
     * and adds it: each kind is warned about once.
     */
    inline void WarnOnce(std::uint32_t& warned, std::uint32_t problem, ILogger& logger, const std::string& message)
    {
        if ((warned & problem) != 0)
        {
            return;
        }
        warned |= problem;
        logger.Warn(message);
    }
} // namespace Fenrir::Detail
