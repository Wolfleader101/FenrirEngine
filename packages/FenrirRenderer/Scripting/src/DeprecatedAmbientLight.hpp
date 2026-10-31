#pragma once

#include "FenrirScripting/ComponentRegistry.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief The registration of the stand-in behind C#'s deprecated AmbientLight (ADR-0047): the Sky is the scene's
     * ambient light and C++ has no AmbientLight, but C# keeps the type, with its old layout, until the lighting spec's
     * C# ticket removes it, so scripts keep compiling. Scripts may insert, read and write it; nothing reads it.
     */
    [[nodiscard]] ResourceRegistration DeprecatedAmbientLightResource();
} // namespace Fenrir::Detail
