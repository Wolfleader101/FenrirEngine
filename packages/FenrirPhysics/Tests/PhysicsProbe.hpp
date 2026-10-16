#pragma once

#include <cstddef>

#include "FenrirPhysics/PhysicsWorld.hpp"

namespace Fenrir
{
    /**
     * @brief The tests' one way inside a PhysicsWorld: its diagnostics. PhysicsWorld names this type as a friend, so
     * none of it is part of the interface game code sees, and the tests need none of FenrirPhysics's private headers.
     */
    struct PhysicsProbe
    {
        /** @brief How many distinct backend shapes the world's bodies use: colliders with identical primitive shapes
         * (after scale) share one. */
        [[nodiscard]] static std::size_t DistinctShapeCount(const PhysicsWorld& world)
        {
            return world.DistinctShapeCount();
        }
    };
} // namespace Fenrir
