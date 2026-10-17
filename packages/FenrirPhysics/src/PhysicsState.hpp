#pragma once

// What Physics::Install set up, shared by the Physics owner, the systems it installs (weakly) and, in a build with
// scripting, FenrirPhysicsScripting's native functions (weakly), and FrameFromWorld's warnings (weakly). Private to the
// FenrirPhysics package.

#include <string>

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

    /**
     * @brief Logs a warning through the app physics was last installed on, while that install lasts; nothing without
     * one. For FenrirPhysics code that is given no app, such as FrameFromWorld.
     */
    void WarnThroughInstalledApp(const std::string& message);
} // namespace Fenrir
