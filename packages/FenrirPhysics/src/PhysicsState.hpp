#pragma once

// What Physics::Install set up, shared by the Physics owner and the systems it installs (weakly). Private to the
// FenrirPhysics package.

#include "Backend/Backend.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirPhysics/CollisionLayers.hpp"
#include "FenrirPhysics/Physics.hpp"

namespace Fenrir
{
    struct Detail::PhysicsState
    {
        PhysicsState(App& installedOn, const PhysicsConfig& installedWith)
            : app(&installedOn), config(installedWith), layers(installedWith.collisionLayers, *installedOn.Logger()),
              context(*installedOn.Logger()), meshes(*installedOn.Logger())
        {
        }

        App* app;
        PhysicsConfig config;
        CollisionLayers layers;
        Backend::Context context;
        // After the context, so it ends first: its shapes need the backend's allocator.
        Backend::CollisionMeshes meshes;
    };
} // namespace Fenrir
