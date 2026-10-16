#pragma once

// The C++ half of the scripting demo (the C# half is in scripts/). Everything here exists only when scripting is built in.
//
// What it shows, with the C# side that goes with each piece:
//   Bob          a component C++ declares and C# animates: C++ sets Amplitude, Speed and BaseY, C# writes Phase every frame, and
//                a C++ system reads Phase back (scripts/Bobbing.cs).
//   KeyboardKey  the window's C++ KeyboardKeyEvent, read from C# unchanged (scripts/Controls.cs).
//   Burst        an event C# sends when Space is pressed; C# reads it to make sparks and C++ reads it here (scripts/Sparks.cs).
//   DemoMesh     a component C# adds to make an entity visible, without knowing the renderer (DemoMesh.hpp, scripts/DemoMesh.cs).
//   SceneClick   the editor's SceneClickEvent, a click in the scene view as a ray (scripts/Controls.cs).

#ifdef FENRIR_SCRIPTING

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "DemoMesh.hpp"
#include "Events.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/EngineMappings.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace ScriptingDemo
{
    // A bobbing motion. C++ decides how it looks; C# runs it and reports where in the cycle it is.
    struct Bob
    {
        float Amplitude = 0.0F; // How far it moves from BaseY, in world units.
        float Speed = 0.0F;     // Radians per second.
        float Phase = 0.0F;     // Written by C# every frame.
        float BaseY = 0.0F;     // The height it bobs around.
    };

    // Sent by C# when a burst of sparks starts, read by C# (to make them) and by C++ (see LogBursts).
    struct BurstEvent
    {
        std::int32_t Count = 0;
    };
} // namespace ScriptingDemo

// How these look to C#. The C# structs (scripts/*.cs) must match field for field, or the script assembly refuses to load and says
// which field is wrong.
template <>
struct Iris::StructMapping<ScriptingDemo::Bob>
{
    static constexpr std::string_view typeName = "DemoScripts.Bob";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(ScriptingDemo::Bob, Amplitude), IRIS_FIELD(ScriptingDemo::Bob, Speed),
                           IRIS_FIELD(ScriptingDemo::Bob, Phase), IRIS_FIELD(ScriptingDemo::Bob, BaseY));
};

template <>
struct Iris::StructMapping<ScriptingDemo::BurstEvent>
{
    static constexpr std::string_view typeName = "DemoScripts.Burst";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(ScriptingDemo::BurstEvent, Count));
};

template <>
struct Iris::EnumMapping<InputState>
{
    static constexpr std::string_view typeName = "DemoScripts.InputState";
};

template <>
struct Iris::StructMapping<KeyboardKeyEvent>
{
    static constexpr std::string_view typeName = "DemoScripts.KeyboardKey";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(KeyboardKeyEvent, key, "Key"), IRIS_FIELD_AS(KeyboardKeyEvent, scancode, "Scancode"),
        IRIS_FIELD_AS(KeyboardKeyEvent, repeat, "Repeat"), IRIS_FIELD_AS(KeyboardKeyEvent, state, "State"),
        IRIS_FIELD_AS(KeyboardKeyEvent, mods, "Mods"));
};

template <>
struct Iris::EnumMapping<MeshKind>
{
    static constexpr std::string_view typeName = "DemoScripts.MeshKind";
};

template <>
struct Iris::StructMapping<DemoMesh>
{
    static constexpr std::string_view typeName = "DemoScripts.DemoMesh";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(DemoMesh, Kind), IRIS_FIELD(DemoMesh, Colour));
};

template <>
struct Iris::StructMapping<SceneClickEvent>
{
    static constexpr std::string_view typeName = "DemoScripts.SceneClick";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(SceneClickEvent, origin, "Origin"),
                                                      IRIS_FIELD_AS(SceneClickEvent, direction, "Direction"));
};

namespace ScriptingDemo
{
    // What the demo app gives scripting: the component and events C++ and C# share, as the app's own extension.
    inline Fenrir::ScriptingExtension Extension()
    {
        Fenrir::ScriptingExtension extension;
        extension.name = "DemoApp";
        extension.components = {Fenrir::MakeComponent<Bob>("Demo.Bob"),
                                Fenrir::MakeComponent<DemoMesh>("Demo.DemoMesh")};
        extension.events = {Fenrir::MakeEvent<BurstEvent>("Demo.Burst"),
                            Fenrir::MakeEvent<KeyboardKeyEvent>("Demo.KeyboardKey"),
                            Fenrir::MakeEvent<SceneClickEvent>("Demo.SceneClick")};
        return extension;
    }

    // Makes an entity bob. The C# Bobber system does the moving.
    inline void AttachBob(Fenrir::Entity entity, float baseY)
    {
        Bob bob;
        bob.Amplitude = 0.75F;
        bob.Speed = 2.0F;
        bob.BaseY = baseY;
        entity.AddComponent<Bob>(bob);
    }

    // Reads what C# wrote (Bob::Phase) and shows it in the entity's name, which the editor's hierarchy displays. This is the
    // shared component working in the other direction: C# data, read by C++ with no copying or marshalling.
    inline void ShowBobPhase(Fenrir::App& app)
    {
        static bool saidSo = false;
        app.GetActiveScene().GetEntityList().ForEach<Fenrir::Name, Bob>([&app](Fenrir::Name& name, Bob& bob) {
            if (!saidSo && bob.Phase > 0.0F)
            {
                saidSo = true;
                app.Logger()->Info("C++ read Bob.Phase = {:.2f}, which C# wrote into the same component.", bob.Phase);
            }
            char label[64];
            std::snprintf(label, sizeof(label), "Backpack 2 (bob phase %.2f)", bob.Phase);
            name.Set(label);
        });
    }

    // Logs every burst C# sends, once each: the system's own reader remembers which it has seen.
    inline Fenrir::SystemFunc LogBursts()
    {
        return [bursts = Fenrir::EventReader<BurstEvent>{}](Fenrir::App& app) mutable {
            for (const BurstEvent& burst : bursts.Read(app))
            {
                app.Logger()->Info("C++ saw a burst of {} sparks sent from C#.", burst.Count);
            }
        };
    }

    // Plays the demo without anyone at the keyboard, for showing it off unattended and for checking it still works: with the
    // environment variable FENRIR_DEMO_AUTOPLAY set, it sends the same KeyboardKeyEvent the window sends, as if R and then
    // Space were pressed. Then, for the C# physics scene (when physics is built in): F3 to go to it, a click at the middle
    // of its box pyramid (the SceneClickEvent the editor sends, from where the camera starts), J to kick its bodies, F3 to
    // go back and F3 to return. Without the variable it does nothing.
    inline Fenrir::SystemFunc AutoPlay()
    {
        auto frame = std::make_shared<int>(0);
        return [frame](Fenrir::App& app) {
            static const bool enabled = std::getenv("FENRIR_DEMO_AUTOPLAY") != nullptr;
            if (!enabled)
            {
                return;
            }
            constexpr int keyR = 82;
            constexpr int keySpace = 32;
            constexpr int keyJ = 74;
            constexpr int keyF3 = 292;
            const auto press = [&app](int key) { app.SendEvent(KeyboardKeyEvent{key, 0, 0, InputState::Pressed, 0}); };
            ++*frame;
            switch (*frame)
            {
            case 120:
                press(keyR);
                break;
            case 240:
                press(keySpace);
                break;
            case 360: // to the C# physics scene
            case 840: // back
            case 960: // and to it again
                press(keyF3);
                break;
            case 480:
                app.SendEvent(SceneClickEvent{Fenrir::Math::Vec3(0.0f),
                                              Fenrir::Math::Normalized(Fenrir::Math::Vec3(0.0f, -2.5f, -12.0f))});
                break;
            case 720:
                press(keyJ);
                break;
            default:
                break;
            }
        };
    }
} // namespace ScriptingDemo

#endif // FENRIR_SCRIPTING
