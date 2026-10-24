#pragma once

#include <cstdint>

#include "FenrirDemoScaffold/DemoScaffold.hpp"

// PhysicsDemo's scenes, the legacy DemoApp's physics scenes ported to the demo scaffold, in the order of its scene
// list: each C++ scene, then its C# twin. Each scene is a source file of its own (its C# twin's entry beside it); the
// C# half of a C# scene is in scripts/.

namespace Fenrir
{
    class App;
    class Physics;
} // namespace Fenrir

namespace PhysicsDemo
{
    /**
     * @brief Playground (C++), the legacy Default scene's physics: a box stack on a ground, a kinematic paddle game
     * code sweeps through it in Tick, and a static backpack with a triangle-mesh collider cooked from its model, with
     * boxes dropped onto it. Adds its Tick system to `app`.
     */
    [[nodiscard]] FenrirDemo::DemoScene Playground(Fenrir::App& app, Fenrir::Physics& physics);

    /** @brief Playground (C#)'s number, which its C# systems know it by (scripts/Playground.cs). */
    inline constexpr std::uint32_t PlaygroundCSharpScene = 1;

    /**
     * @brief Playground (C#), the legacy Physics (C#) scene, built and run entirely by C# (scripts/Playground.cs): a
     * box pyramid, bouncy spheres, a paddle and a trigger zone; J kicks the bodies, a left click casts a ray.
     */
    [[nodiscard]] FenrirDemo::DemoScene PlaygroundCSharp();

    /**
     * @brief Joints (C++): a door hinged to a static post and a chain of eight links from a world point, with the
     * legacy scene's keys for every joint edge case. Adds its Update system to `app`.
     */
    [[nodiscard]] FenrirDemo::DemoScene Joints(Fenrir::App& app);

    /** @brief Joints (C#)'s number, which its C# systems know it by (scripts/Joints.cs). */
    inline constexpr std::uint32_t JointsCSharpScene = 2;

    /**
     * @brief Joints (C#), built and run entirely by C# (scripts/Joints.cs): a breakable bridge, a lift on a slider, a
     * spring rope and a swing-twist limb.
     */
    [[nodiscard]] FenrirDemo::DemoScene JointsCSharp();

    /**
     * @brief Character (C++): a character body walked through a small level by the keys or an autopilot, with a follow
     * camera of its own. Adds its Tick and Update systems to `app`.
     */
    [[nodiscard]] FenrirDemo::DemoScene Character(Fenrir::App& app, Fenrir::Physics& physics);

    /** @brief Character (C#)'s number, which its C# systems know it by (scripts/Character.cs). */
    inline constexpr std::uint32_t CharacterCSharpScene = 3;

    /**
     * @brief Character (C#), built and driven entirely by C# (scripts/Character.cs): a character body on a level of
     * its own with a slide ramp, ice, a crouch tunnel, a trigger pad, a lift, a ghost pane and a test wall, and its own
     * follow camera.
     */
    [[nodiscard]] FenrirDemo::DemoScene CharacterCSharp();

    /**
     * @brief Vehicles (C++): a car towing a trailer round a track of bumps, a ramp, ice and grass lanes, crates, a
     * ferry and a parking slope, driven by the keys, a gamepad or an autopilot, with a chase camera; its driver gets
     * out and walks. Adds its Tick and Update systems to `app`.
     */
    [[nodiscard]] FenrirDemo::DemoScene Vehicles(Fenrir::App& app);

    /**
     * @brief Motorcycle (C++): Jolt's sample motorcycle on open tarmac, standing at rest and leaning into turns, with
     * the chase camera. Adds its Tick and Update systems to `app`.
     */
    [[nodiscard]] FenrirDemo::DemoScene Motorcycle(Fenrir::App& app);

    /**
     * @brief Tank (C++): a tank on two tracks of nine wheels each, pivoting on the spot when stopped, on open tarmac
     * with a row of crates, with the chase camera. Adds its Tick and Update systems to `app`.
     */
    [[nodiscard]] FenrirDemo::DemoScene Tank(Fenrir::App& app);

    /** @brief Vehicles (C#)'s number, which its C# systems know it by (scripts/Vehicles.cs). */
    inline constexpr std::uint32_t VehiclesCSharpScene = 4;

    /**
     * @brief Vehicles (C#), built and driven entirely by C# (scripts/Vehicles.cs): a car towing a trailer on a track
     * of its own, with its own chase camera.
     */
    [[nodiscard]] FenrirDemo::DemoScene VehiclesCSharp();
} // namespace PhysicsDemo
