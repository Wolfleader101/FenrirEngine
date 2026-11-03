#pragma once

#include <span>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class App;
    class AssetStore;
    class Scene;
} // namespace Fenrir

// What PhysicsDemo's C++ scenes share: how their bodies look, the readouts the legacy editor's hierarchy showed, and
// F2.
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

    /**
     * @brief Shows each entity's Name as debug screen text, one line each from the window's top-left: the legacy
     * scenes write their live readings into names, which the legacy editor's hierarchy showed. A destroyed entity is
     * skipped.
     */
    void ShowReadouts(Fenrir::Scene& scene, std::span<const Fenrir::Entity> entities);

    /**
     * @brief Adds PhysicsDemo's F2: each press steps the physics debug categories (off, colliders, contacts,
     * velocities, bounds, joints, characters, vehicles, all, and round again), logged, and applies them to the active
     * scene's physics world. The categories are the app's, so a scene entered later gets them once its world exists;
     * what a scene's own code sets lasts until the next F2 or scene.
     */
    void AddDebugCategoryCycling(Fenrir::App& app);
} // namespace PhysicsDemo
