#pragma once

// The C++ half of the scenes C# builds and runs. C++ only makes each scene, puts one entity in it with a CSharpScene
// marker naming the scene, so that the scene's C# system builds there, and registers it with the scene switcher. It
// creates no physics bodies. Exists only when physics and scripting are both built in.
//
//   Physics (C#)   F3, built by scripts/Physics/Playground.cs.
//   Joints (C#)    F4, after Joints (C++), built by scripts/Physics/Joints.cs.
//   Character (C#) F5, after Character (C++), built by scripts/Physics/Character.cs.

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
    inline constexpr std::int32_t physicsId = 1;   // Playground.SceneId
    inline constexpr std::int32_t jointsId = 2;    // JointsScene.SceneId
    inline constexpr std::int32_t characterId = 3; // CharacterScene.SceneId

    inline constexpr const char* physicsName = "Physics (C#)";
    inline constexpr const char* jointsName = "Joints (C#)";
    inline constexpr const char* characterName = "Character (C#)";

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

    /** @brief Joints (C#), as the switcher steps to it: F4 from Joints (C++), and on to Default. Register it after
     * Joints (C++). */
    inline DemoScene Joints()
    {
        constexpr int key0 = 48;
        constexpr int key1 = 49;
        constexpr int key2 = 50;
        constexpr int keyB = 66;
        constexpr int keyK = 75;
        constexpr int keyR = 82;
        return DemoScene{.name = jointsName,
                         .familyKey = DemoFamilyKey::joints,
                         .keyHelp =
                             "A bridge, a lift, a spring rope and a limb, all built by C#. B drops a weight on the "
                             "bridge, whose middle hinge breaks over 250 N; R repairs broken joints. K swaps the "
                             "middle joint between hinge and fixed. 1 cycles the lift's motor (off, up, down, half "
                             "way), 2 raises the limb and lowers it. 0 logs every joint's state. F2 cycles the "
                             "physics debug lines, F4 goes on.",
                         .smokeKeys = {key0, keyK, keyK, keyB, key0, keyR, key1, key1, key1, key1, key2, key2, key0}};
    }

    /** @brief Character (C#), as the switcher steps to it: F5 from Character (C++), and on to Default. Register it
     * after Character (C++). */
    inline DemoScene Character()
    {
        // GLFW's key codes, without GLFW's header (it must follow glad's).
        constexpr int keySpace = 32;
        constexpr int key1 = 49;
        constexpr int keyC = 67;
        constexpr int keyE = 69;
        constexpr int keyF = 70;
        constexpr int keyK = 75;
        constexpr int keyL = 76;
        constexpr int keyP = 80;
        constexpr int keyQ = 81;
        constexpr int keyT = 84;
        constexpr int keyX = 88;
        constexpr int keyLeftShift = 340;
        DemoScene scene{
            .name = characterName,
            .familyKey = DemoFamilyKey::characterController,
            .keyHelp = "A character body built and driven by C#. WASD or the arrows walk (relative to the "
                       "camera), Shift runs, Space jumps, Q and E turn the camera, C frees the camera and snaps it "
                       "back, X toggles crouch. A slide ramp to the west, ice to the east, a crouch tunnel to the "
                       "south, a trigger pad to the south-west, a lift to the north-east, and to the north a ghost "
                       "pane the character passes through and a test wall. F dashes 8 m ahead (MoveCast), K puts "
                       "the character half into the test wall and pushes it out (Penetrations, Depenetrate). T "
                       "teleports to the start, 1 toggles StopOnSlope (off at the start), L logs the state, P walks "
                       "the route by itself. F5 goes on.",
            .smokeKeys = {keyP}};
        // The autopilot walks the whole route while L logs the state, then stops, each key is tapped twice (T last,
        // so F dashes from the start, through the ghost pane to the test wall), F and K once, and T ends it.
        scene.smokeKeys.insert(scene.smokeKeys.end(), 40, keyL);
        scene.smokeKeys.push_back(keyP);
        for (const int key : {keyC, keySpace, keyQ, keyE, keyLeftShift, keyX, key1, keyT})
        {
            scene.smokeKeys.insert(scene.smokeKeys.end(), {key, key});
        }
        scene.smokeKeys.insert(scene.smokeKeys.end(), {keyF, keyK, keyL, keyT});
        return scene;
    }
} // namespace CSharpScenes

#endif // FENRIR_PHYSICS && FENRIR_SCRIPTING
