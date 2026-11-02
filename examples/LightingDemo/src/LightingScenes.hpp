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

    /**
     * @brief Many lights: a dark room of PBR floor tiles, walls, columns and spheres lit by up to 256 moving coloured
     * point and spot lights (every fourth a spot), each with a glowing marker; the panel sets how many, their
     * intensities and range.
     */
    [[nodiscard]] FenrirDemo::DemoScene ManyLights();

    /**
     * @brief Showroom: a dark room whose polished floor turns rougher from left to right, lit by rect lights (a neon
     * strip, a panel and a two-sided sign), with the fetched helmets on plinths; the panel sizes, dims and turns each
     * light one- or two-sided.
     */
    [[nodiscard]] FenrirDemo::DemoScene Showroom();

    /**
     * @brief Asteroid ring: a planet ringed by 100k fetched Poly Haven rocks of three kinds, one InstancedMeshRenderer
     * each, under a starfield the demo makes; beside the ring, 5k rock entities merged automatically and an emissive
     * station whose point lights light them. The panel sets the ring's count and turns merging off and on, and shows
     * draws and instances. Without fetched assets, spheres and cubes stand in.
     */
    [[nodiscard]] FenrirDemo::DemoScene AsteroidRing();

    /**
     * @brief Forest clearing: a 40-metre clearing in late-afternoon sun, a winding path, a mossy boulder and a fallen
     * log, ringed by a dense forest of fetched Poly Haven firs and broadleaves thinned with distance, with ferns,
     * shrubs, rocks and deadwood, about 20k copies in instance lists; about 200k generated grass blades sway through
     * the demo's wind shader over its blended forest-floor shader, under the sun's cascaded shadows. The panel thins
     * the forest and hides the grass. Without fetched assets, the grass grows among box trunks and sphere boulders.
     */
    [[nodiscard]] FenrirDemo::DemoScene ForestClearing();

    /**
     * @brief Neon city at night: a grid of generated towers on podiums and lower blocks, every tier one copy drawn by
     * the demo's facade shader (fetched concrete, brick and cladding walls; punched, curtain-wall or ribbon windows
     * with rooms behind them by interior mapping, lit by the copy colour; neon edge strips on many towers), over wet
     * asphalt streets (fetched) whose puddles mirror the lights: rect-light neon signs and billboards lettered by the
     * demo's sign shader, coloured spot uplights washing the towers, point-light street lamps and the spot headlights
     * of instanced cars, over a thousand lights, clustered, under the demo's city glow or a dim night panorama
     * (fetched). The panel sets the lights' intensities and the puddles, stops the traffic, turns clustering and vsync
     * off and shows the light count and the frame time. Without fetched assets the streets and walls are plain.
     */
    [[nodiscard]] FenrirDemo::DemoScene NeonCity();

    /**
     * @brief Sponza: Khronos's Sponza atrium (fetched) under a partly cloudy Poly Haven sky (fetched), lit by the sun
     * through the open roof, its shadows in cascades, and by four flickering torches in the galleries, each a shadowed
     * point light; GTAO grounds the colonnades and the flames bloom. The panel sets the sun's and the torches'
     * intensities and turns the torches' shadows off. Without fetched assets, plain colonnades and walls stand in.
     */
    [[nodiscard]] FenrirDemo::DemoScene Sponza();

    /**
     * @brief Many lights (C#): the many-lights room made and run by scripts/ManyLights.cs, its lights C# components and
     * its markers one instance list C# rewrites each frame; the lighting panel stays C++'s. In the list only with
     * scripting.
     */
    [[nodiscard]] FenrirDemo::DemoScene ManyLightsCSharp();
} // namespace LightingDemo
