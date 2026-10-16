#pragma once

// What Physics::Install set up, shared by the Physics owner, the systems it installs (weakly) and, in a build with
// scripting, FenrirPhysicsScripting's native functions (weakly). Private to the FenrirPhysics package.

#include "Backend/Backend.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirPhysics/CollisionLayers.hpp"
#include "FenrirPhysics/Physics.hpp"

namespace Fenrir
{
    struct Physics::State
    {
        State(App& installedOn, const PhysicsConfig& installedWith)
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
