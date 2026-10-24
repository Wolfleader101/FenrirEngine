#pragma once

// The physics scripting tests' harness, shared by PhysicsScriptingTests.cpp and the generated defaults test
// (Generated/FenrirPhysicsDefaultsTests.cpp): an App with no window, debug drawing and physics installed and scripting
// running the physics fixture script assembly with physics's extension.

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>

#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace Fenrir::Testing
{
    /** @brief The path in environment variable `name`, made absolute, or empty (saying so) when it is not set. */
    inline std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the physics scripting tests need the fixture assembly.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    /** @brief The fixture script assembly CTest names. */
    inline const std::filesystem::path physicsFixture = FromEnvironment("FENRIR_TEST_PHYSICS_FIXTURE");

    /** @brief Physics with interpolation off, so a body's Transform after a whole-tick frame is its latest step pose.
     */
    inline PhysicsConfig StepPosesOnly()
    {
        PhysicsConfig config;
        config.interpolate = false;
        return config;
    }

    /**
     * @brief An App with debug drawing and physics installed and scripting running the fixture with physics's
     * extension. Members end in reverse order: scripting, then physics, then debug drawing, then the app.
     */
    struct PhysicsScriptingApp
    {
        PhysicsScriptingApp()
            : debugDrawing(DebugDrawing::Install(headless.Get())),
              physics(Physics::Install(headless.Get(), StepPosesOnly())),
              scripting(Scripting::Install(headless.Get(), Config()))
        {
            INFO("The fixture did not load with physics's extension");
            REQUIRE(scripting.HasScripts());
        }

        static ScriptingConfig Config()
        {
            ScriptingConfig config;
            config.scriptAssembly = physicsFixture;
            config.hotReload = false;
            config.extensions = {PhysicsScriptingExtension()};
            return config;
        }

        HeadlessApp headless;
        DebugDrawing debugDrawing;
        Physics physics;
        Scripting scripting;

        EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        // An entity with only the engine's components, at `position`.
        std::uint32_t Place(const Math::Vec3& position)
        {
            Entity entity = Entities().CreateEntity();
            entity.GetComponent<Transform>() =
                Transform(position, Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), Math::Vec3(1.0f));
            return entity.GetId();
        }

        void Run(int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                headless.Get().RunFrame(HeadlessApp::FrameDelta);
            }
        }

        // Plays back the structural changes the fixture recorded, as the system runner does after a system.
        void ApplyChanges()
        {
            ScriptingProbe::ApplyChanges(scripting);
        }

        // Calls a static method of a type in the fixture through Iris.
        template <typename Result, typename... Args>
        Result Call(const char* typeName, const char* method, Args... arguments)
        {
            auto* component = ScriptingProbe::Component(scripting);
            {
                INFO("There is no loaded script assembly");
                REQUIRE(component != nullptr);
            }
            const auto type = component->FindType(typeName);
            {
                INFO(std::string("The fixture type was not found: ") + type.diagnostic);
                REQUIRE(type.Succeeded());
            }
            const auto handle = type.value->FindMethod<Result(Args...)>(method);
            {
                INFO(std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
                REQUIRE(handle.Succeeded());
            }
            const auto result = handle.value->Invoke(arguments...);
            {
                INFO(std::string("The fixture method ") + method + " failed: " + result.diagnostic);
                REQUIRE(result.Succeeded());
            }
            if constexpr (!std::is_void_v<Result>)
            {
                return *result.value;
            }
        }
    };
} // namespace Fenrir::Testing
