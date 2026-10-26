#pragma once

// The physics scripting tests' part of FenrirScripting's ExtensionTestApp, shared by PhysicsScriptingTests.cpp and the
// generated defaults test (Generated/FenrirPhysicsDefaultsTests.cpp): the fixture CTest names in
// FENRIR_TEST_PHYSICS_FIXTURE, debug drawing and physics installed, and physics's extension.

#include "ExtensionTestApp.hpp"

#include <cstdint>
#include <vector>

#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"

namespace Fenrir::Testing
{
    /** @brief Physics with interpolation off, so a body's Transform after a whole-tick frame is its latest step pose.
     */
    inline PhysicsConfig StepPosesOnly()
    {
        PhysicsConfig config;
        config.interpolate = false;
        return config;
    }

    /** @brief Debug drawing and physics, installed in that order and ending in reverse. */
    struct PhysicsPackages
    {
        explicit PhysicsPackages(App& app)
            : debugDrawing(DebugDrawing::Install(app)), physics(Physics::Install(app, StepPosesOnly()))
        {
        }

        DebugDrawing debugDrawing;
        Physics physics;
    };

    /** @brief An App with debug drawing and physics installed and scripting running the fixture with physics's
     * extension. */
    struct PhysicsScriptingApp : ExtensionTestApp<PhysicsPackages>
    {
        PhysicsScriptingApp() : ExtensionTestApp(Config())
        {
        }

        /** @brief Scripting running the fixture with physics's extension: what the drifted-mirror tests edit. */
        static ScriptingConfig Config()
        {
            return FixtureConfig(FixtureFromEnvironment("FENRIR_TEST_PHYSICS_FIXTURE"), {PhysicsScriptingExtension()});
        }

        // An entity with only the engine's components, at `position`.
        std::uint32_t Place(const Math::Vec3& position)
        {
            Entity entity = Entities().CreateEntity();
            entity.GetComponent<Transform>() =
                Transform(position, Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), Math::Vec3(1.0f));
            return entity.GetId();
        }
    };
} // namespace Fenrir::Testing
