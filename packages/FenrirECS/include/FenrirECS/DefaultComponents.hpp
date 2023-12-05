#pragma once

#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    struct Transform
    {
        Fenrir::Math::Vec3 pos;
        Fenrir::Math::Quat rot;
        Fenrir::Math::Vec3 scale;
    };

    struct Name
    {
        // max name length is 255 characters
        char name[256] = {0};
    };

} // namespace Fenrir