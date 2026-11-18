#pragma once

#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class App;
    class AssetStore;
    class Scene;
} // namespace Fenrir

// What PhysicsDemo's C++ scenes share: how their bodies look, the readouts the legacy editor's hierarchy showed, and
// F2 with its Debug lines choice.
namespace PhysicsDemo
{
    /**
     * @brief How a C++ scene's bodies look, as the legacy DemoApp drew them: the shared unit cube and sphere (one unit
     * across, so an entity's Transform scale is its size, matching a collider's default shape) in one flat colour each,
     * lit by the scene's sun under the skybox. Everything it loads or makes is owned by the scene.
     */
    class SceneLooks
    {
      public:
        /**
         * @brief Loads the cube, the sphere, builtin:lit and the skybox for `scene`, and gives it a directional light
         * and the skybox as its Sky, which lights it too. Logs an error if any did not load.
         */
        SceneLooks(Fenrir::App& app, Fenrir::Scene& scene);

        /** @brief Draws `entity` as the unit cube in `colour`, red, green and blue in [0, 1] as the legacy scenes gave
         * them (sRGB). */
        void Cube(Fenrir::Entity& entity, const Fenrir::Math::Vec3& colour);

        /** @brief Draws `entity` as the unit sphere (radius 0.5) in `colour`. */
        void Sphere(Fenrir::Entity& entity, const Fenrir::Math::Vec3& colour);

        /**
         * @brief The scene's material in `colour`, made the first time a colour is asked for and shared by every
         * entity drawn in it.
         */
        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Material> Material(const Fenrir::Math::Vec3& colour);

      private:
        Fenrir::AssetStore* m_store;
        Fenrir::Scene* m_scene;
        Fenrir::AssetHandle<Fenrir::Mesh> m_cube;
        Fenrir::AssetHandle<Fenrir::Mesh> m_sphere;
        Fenrir::AssetHandle<Fenrir::Shader> m_lit;
        std::vector<std::pair<Fenrir::Math::Vec3, Fenrir::AssetHandle<Fenrir::Material>>> m_materials;
    };

    /**
     * @brief A Camera entity's rotation looking along the legacy DemoCamera's yaw and pitch: degrees about world +Y
     * where -90 looks along -Z (and 0 along +X), and degrees above the horizon.
     */
    [[nodiscard]] Fenrir::Math::Quat LookRotation(float yawDegrees, float pitchDegrees);

    /** @brief One readout of ShowReadouts: the row's label, and the entity whose Name holds the reading. */
    struct Reading
    {
        std::string_view label;
        Fenrir::Entity entity;
    };

    /**
     * @brief The value a readout labelled `label` shows for an entity named `name`: the name without a leading
     * "<label>: " or "<label> ", which the legacy scenes' names start with, or else the whole name.
     */
    [[nodiscard]] std::string_view ReadingValue(std::string_view name, std::string_view label);

    /**
     * @brief Shows each entity's Name as a readout in the active scene's "Readings" section of the demo panel: the
     * legacy scenes write their live readings into names, which the legacy editor's hierarchy showed. Called each frame
     * from the scene's `Update`; the section and a readout per label are made the first time they are asked for, and go
     * with the scene. A destroyed entity's readout is hidden.
     */
    void ShowReadouts(Fenrir::App& app, std::span<const Reading> readings);

    /**
     * @brief Adds PhysicsDemo's F2 and, with the demo UI kit, its "Debug lines" choice (a switch row) in the demo
     * panel's app section "Physics": each press steps the physics debug categories (off, colliders, contacts,
     * velocities, bounds, joints, characters, vehicles, all, and round again), the choice picks one, and either is
     * logged and applied to the active scene's physics world. The categories are the app's, so a scene entered later
     * gets them once its world exists; what a scene's own code sets lasts until the next F2, choice or scene. Run after
     * DemoScaffold::Install, which adds the kit.
     */
    void AddDebugCategoryCycling(Fenrir::App& app);

    /** @brief Whether any of `keys` was pressed this frame: a demo tutorial step's `done` for the keys it teaches. */
    [[nodiscard]] bool AnyJustPressed(Fenrir::App& app, std::initializer_list<Fenrir::Key> keys);
} // namespace PhysicsDemo
