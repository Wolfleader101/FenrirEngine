#pragma once

// The C++ half of the scripting demo (the C# half is in scripts/). Everything here exists only when scripting is built in.
//
// What it shows, with the C# side that goes with each piece:
//   Bob          a component C++ declares and C# animates: C++ sets Amplitude, Speed and BaseY, C# writes Phase every frame, and
//                a C++ system reads Phase back (scripts/Bobbing.cs).
//   KeyboardKey  the window's C++ KeyboardKeyEvent, read from C# unchanged (scripts/Controls.cs).
//   Burst        an event C# sends when Space is pressed; C# reads it to make sparks and C++ reads it here (scripts/Sparks.cs).

#ifdef FENRIR_SCRIPTING

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "Events.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
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

namespace ScriptingDemo
{
    // What the demo app gives scripting: the component and events C++ and C# share, as the app's own extension.
    inline Fenrir::ScriptingExtension Extension()
    {
        Fenrir::ScriptingExtension extension;
        extension.name = "DemoApp";
        extension.components = {Fenrir::MakeComponent<Bob>("Demo.Bob")};
        extension.events = {Fenrir::MakeEvent<BurstEvent>("Demo.Burst"), Fenrir::MakeEvent<KeyboardKeyEvent>("Demo.KeyboardKey")};
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

    // Logs every burst C# sends. An event stays readable for two frames (this frame's and last frame's), so a reader that wants
    // each event once remembers how many of the events it can see are this frame's.
    inline Fenrir::SystemFunc LogBursts()
    {
        auto lastFrameCount = std::make_shared<std::size_t>(0);
        return [lastFrameCount](Fenrir::App& app) {
            const auto& bursts = app.ReadEvents<BurstEvent>();
            const std::size_t thisFrame = bursts.size() >= *lastFrameCount ? bursts.size() - *lastFrameCount : 0;
            for (std::size_t index = bursts.size() - thisFrame; index < bursts.size(); ++index)
            {
                app.Logger()->Info("C++ saw a burst of {} sparks sent from C#.", bursts[index].Count);
            }
            *lastFrameCount = thisFrame;
        };
    }

    // Plays the demo without anyone at the keyboard, for showing it off unattended and for checking it still works: with the
    // environment variable FENRIR_DEMO_AUTOPLAY set, it sends the same KeyboardKeyEvent the window sends, as if R and then
    // Space were pressed. Without the variable it does nothing.
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
            ++*frame;
            if (*frame == 120)
            {
                app.SendEvent(KeyboardKeyEvent{keyR, 0, 0, InputState::Pressed, 0});
            }
            else if (*frame == 240)
            {
                app.SendEvent(KeyboardKeyEvent{keySpace, 0, 0, InputState::Pressed, 0});
            }
        };
    }
} // namespace ScriptingDemo

#endif // FENRIR_SCRIPTING
