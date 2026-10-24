#pragma once

#include <cstdint>

#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

// The C++ half of the scripting showcase (the C# half is in scripts/), the legacy DemoApp's scripting demo on the demo
// scaffold. What it shows, with the C# side that goes with each piece:
//   Bob       a component C++ declares and C# animates: C++ sets Amplitude, Speed and BaseY, C# writes Phase every
//             frame, and a C++ system reads Phase back (scripts/Bobbing.cs).
//   KeyEvent  FenrirInput's C++ key event, read from C# unchanged (scripts/Controls.cs).
//   Burst     an event C# sends when Space is pressed; C# reads it to make sparks and C++ reads it here
//             (scripts/Sparks.cs).

namespace Fenrir
{
    class App;
} // namespace Fenrir

namespace ScriptingDemo
{
    /** @brief A bobbing motion. C++ decides how it looks; C# runs it and reports where in the cycle it is. */
    struct Bob
    {
        /** @brief How far it moves from BaseY, in world units. */
        float Amplitude = 0.0f;
        /** @brief Radians per second. */
        float Speed = 0.0f;
        /** @brief Written by C# every frame. */
        float Phase = 0.0f;
        /** @brief The height it bobs around. */
        float BaseY = 0.0f;
    };

    /** @brief Sent by C# when a burst of sparks starts, read by C# (to make them) and by C++ (AddSystems). */
    struct BurstEvent
    {
        std::int32_t Count = 0;
    };

    /** @brief What the app gives scripting: the component and event C++ and C# share, as the app's own extension. */
    [[nodiscard]] Fenrir::ScriptingExtension Extension();

    /**
     * @brief "Showcase": the two backpacks, drawn by MeshRenderer, which C# spins and bobs; R reverses the spin and
     * Space makes sparks.
     */
    [[nodiscard]] FenrirDemo::DemoScene Showcase();

    /**
     * @brief Adds the C++ systems, in Update: call it after DemoScaffold::Install, so they run after the C# systems and
     * see what C# just wrote. One shows Bob.Phase in the entity's Name, one logs each Burst once, and one draws the
     * names of the backpacks and the sparks as world text, where the legacy editor's hierarchy showed them.
     */
    void AddSystems(Fenrir::App& app);
} // namespace ScriptingDemo
