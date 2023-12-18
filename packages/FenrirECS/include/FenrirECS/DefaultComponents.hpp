#pragma once

#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    struct Transform
    {
        Transform() = default;

        Transform(const Math::Vec3& pos, const Math::Quat& rot, const Math::Vec3& scale)
            : pos(pos), rot(rot), scale(scale)
        {
        }

        Fenrir::Math::Vec3 pos = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
        Fenrir::Math::Quat rot = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Fenrir::Math::Vec3 scale = Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f);
    };

    struct Name
    {
        // max name length is 255 characters
        char name[256] = {0};
    };

} // namespace Fenrir