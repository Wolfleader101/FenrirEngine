#pragma once

// The C++ half of the "Physics (C#)" scene, whose whole content is built and run by a C# script
// (scripts/Physics/Playground.cs). C++ only makes the scene, puts one entity in it with a CSharpPhysicsScene component
// to tell the script to build there, and switches scenes on F3. It creates no physics bodies. Exists only when physics
// and scripting are both built in.

#if defined(FENRIR_PHYSICS) && defined(FENRIR_SCRIPTING)

#include <cstdint>
#include <string>

#include "Events.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/Scripting.hpp"

/** @brief The marker the C# Playground system looks for. It builds the scene where it finds one with Built == 0, and
 * sets Built, which stays in the ECS, so the scene is built once, even across a hot reload. */
struct CSharpPhysicsScene
{
    std::int32_t Built = 0;
};

template <>
struct Iris::StructMapping<CSharpPhysicsScene>
{
    static constexpr std::string_view typeName = "DemoScripts.CSharpPhysicsScene";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(CSharpPhysicsScene, Built));
};

namespace CSharpPhysics
{
    inline constexpr const char* sceneName = "Physics (C#)";
    inline constexpr const char* firstSceneName = "Default"; // The scene the App starts in.

    /** @brief The marker, as the app's scripting extension shares it with C#. */
    inline Fenrir::ComponentRegistration Component()
    {
        return Fenrir::MakeComponent<CSharpPhysicsScene>("Demo.CSharpPhysicsScene");
    }

    /** @brief Makes the scene and its one entity. The scene stays inactive until F3. */
    inline void CreateScene(Fenrir::App& app)
    {
        Fenrir::Entity marker = app.CreateScene(sceneName).GetEntityList().CreateEntity();
        marker.GetComponent<Fenrir::Name>().Set("Physics (C#) scene");
        marker.AddComponent<CSharpPhysicsScene>();
    }

    /** @brief In PreUpdate, before anything of the frame has run: F3 swaps between the first scene and the C# one.
     * Physics steps only the active scene, so the other one's world waits where it was until it is active again. */
    inline Fenrir::SystemFunc SwitchOnF3()
    {
        return [keys = Fenrir::EventReader<KeyboardKeyEvent>{}](Fenrir::App& app) mutable {
            for (const KeyboardKeyEvent& event : keys.Read(app))
            {
                constexpr int keyF3 = 292; // GLFW_KEY_F3, without GLFW's header (it must follow glad's).
                if (event.key != keyF3 || event.state != InputState::Pressed)
                {
                    continue;
                }
                const bool inFirst = app.GetActiveScene().GetName() == firstSceneName;
                const std::string next = inFirst ? sceneName : firstSceneName;
                app.ChangeActiveScene(next);
                app.Logger()->Info("Scene: {}", next);
            }
        };
    }
} // namespace CSharpPhysics

#endif // FENRIR_PHYSICS && FENRIR_SCRIPTING
