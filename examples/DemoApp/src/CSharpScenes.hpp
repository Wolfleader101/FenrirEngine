#pragma once

// The C++ half of the scenes C# builds and runs. C++ only makes each scene, puts one entity in it with a CSharpScene
// marker naming the scene, so that the scene's C# system builds there, and registers it with the scene switcher. It
// creates no physics bodies. Exists only when physics and scripting are both built in.
//
//   Physics (C#)   F3, built by scripts/Physics/Playground.cs.

#if defined(FENRIR_PHYSICS) && defined(FENRIR_SCRIPTING)

#include <cstdint>
#include <string>

#include "DemoScenes.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "ScriptingDemo.hpp"

namespace CSharpScenes
{
    // The ids in CSharpScene::Scene. Each C# scene declares its own as the same number.
    inline constexpr std::int32_t physicsId = 1; // Playground.SceneId

    inline constexpr const char* physicsName = "Physics (C#)";

    /** @brief Makes the scene `name` and its one entity, marked for the C# system that builds scene `id`. The scene
     * stays inactive until the switcher steps to it. */
    inline void Create(Fenrir::App& app, const std::string& name, std::int32_t id)
    {
        Fenrir::Entity marker = app.CreateScene(name).GetEntityList().CreateEntity();
        marker.GetComponent<Fenrir::Name>().Set((name + " scene").c_str());
        marker.AddComponent<ScriptingDemo::CSharpScene>(ScriptingDemo::CSharpScene{.Scene = id});
    }

    /** @brief Physics (C#), as the switcher steps to it: F3 from Default and back. */
    inline DemoScene Physics()
    {
        constexpr int keyJ = 74;
        return DemoScene{.name = physicsName,
                         .familyKey = DemoFamilyKey::physicsCSharp,
                         .keyHelp = "J kicks the bodies, a left click casts a ray, F2 cycles the debug lines, F3 goes "
                                    "back.",
                         .smokeKeys = {keyJ}};
    }
} // namespace CSharpScenes

#endif // FENRIR_PHYSICS && FENRIR_SCRIPTING
