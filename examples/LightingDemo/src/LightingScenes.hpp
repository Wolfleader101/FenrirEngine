#pragma once

#include "FenrirDemoScaffold/DemoScaffold.hpp"

// LightingDemo's scenes, in the order of its scene list (README.md). Each lighting ticket with a scene adds it here.
namespace LightingDemo
{
    /**
     * @brief Material grid: seven by seven spheres in builtin:lit, rough to the right and metallic upwards, lit by a
     * committed Poly Haven studio panorama alone or with a sun; the panel edits their base colour.
     */
    [[nodiscard]] FenrirDemo::DemoScene MaterialGrid();

    /**
     * @brief Showcase models: Khronos's DamagedHelmet, FlightHelmet and SciFiHelmet in a row under a choice of three
     * Poly Haven panoramas, all fetched (ADR-0052); without them the scene is empty and says how to fetch them.
     */
    [[nodiscard]] FenrirDemo::DemoScene ShowcaseModels();
} // namespace LightingDemo
