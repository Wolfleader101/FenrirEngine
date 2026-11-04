#pragma once

#include <vector>

#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief An instance list as plain data (ADR-0051): many copies of one mesh, each a transform relative to the
     * entity that draws them through an InstancedMeshRenderer, with an optional colour each, which builtin:lit and
     * builtin:unlit multiply their base colour by.
     *
     * An asset rather than a vector in the component, so the renderer can tell when it changed: it uploads a list once,
     * and again only when its revision moves on. Make one with `Add<InstanceList>`, and edit it with `Replace`, which
     * bumps the revision. `colours` is empty, every copy then white, or holds one colour per transform.
     */
    struct InstanceListData
    {
        /** @brief Each copy's transform, relative to the drawing entity's world matrix. */
        std::vector<Math::Mat4> transforms;
        /** @brief Each copy's colour, linear, in the order of `transforms`; empty for white. */
        std::vector<Colour> colours;
    };
} // namespace Fenrir
