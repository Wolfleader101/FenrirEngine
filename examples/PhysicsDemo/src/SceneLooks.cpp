#include "SceneLooks.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <string>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"

namespace PhysicsDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        // The sRGB transfer function's inverse (IEC 61966-2-1): the legacy colours were shown unconverted, as sRGB.
        float SrgbToLinear(float encoded)
        {
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

        /** @brief Marks a scene whose physics world has been given the app's debug categories. */
        struct DebugCategoriesApplied
        {
        };

        struct DebugMode
        {
            Fenrir::PhysicsDebugCategories categories;
            const char* name;
        };

        // What F2 steps through, starting from none.
        constexpr std::array<DebugMode, 9> DebugModes = {
            DebugMode{Fenrir::PhysicsDebugCategories::None, "off"},
            DebugMode{Fenrir::PhysicsDebugCategories::Colliders, "colliders"},
            DebugMode{Fenrir::PhysicsDebugCategories::Contacts, "contacts"},
            DebugMode{Fenrir::PhysicsDebugCategories::Velocities, "velocities"},
            DebugMode{Fenrir::PhysicsDebugCategories::Bounds, "bounds"},
            DebugMode{Fenrir::PhysicsDebugCategories::Joints, "joints"},
            DebugMode{Fenrir::PhysicsDebugCategories::Characters, "characters"},
            DebugMode{Fenrir::PhysicsDebugCategories::Vehicles, "vehicles"},
            DebugMode{Fenrir::PhysicsDebugCategories::All, "all"},
        };
    } // namespace

    SceneLooks::SceneLooks(Fenrir::App& app, Fenrir::Scene& scene)
        : m_store(&app.GetResource<Fenrir::AssetStore>()), m_scene(&scene)
    {
        const auto cube = m_store->Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
        const auto sphere = m_store->Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
        const auto lit = m_store->Load<Fenrir::Shader>("builtin:lit", scene);
        const auto skybox = m_store->Load<Fenrir::Image>("skybox/skybox.cubemap", scene);
        if (!cube.Ok() || !sphere.Ok() || !lit.Ok() || !skybox.Ok())
        {
            app.Logger()->Error("{}: the cube, sphere, lit shader or skybox did not load from the asset root",
                                scene.GetName());
        }
        m_cube = cube.handle;
        m_sphere = sphere.handle;
        m_lit = lit.handle;

        // The legacy demo's sun shone down along (-0.2, -1, -0.3); this one shines along its -Z.
        Fenrir::Entity sun = scene.GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Name>().Set("Sun");
        sun.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.6f, Vec3(0.0f, 1.0f, 0.0f)) *
                                                    Fenrir::Math::AngleAxis(-1.1f, Vec3(1.0f, 0.0f, 0.0f));
        sun.AddComponent<Fenrir::DirectionalLight>();
        Fenrir::AmbientLight ambient;
        ambient.colour = Fenrir::Colour{0.2f, 0.2f, 0.2f, 1.0f};
        scene.AddResource<Fenrir::AmbientLight>(ambient);
        scene.AddResource<Fenrir::Sky>(Fenrir::CubemapSky{skybox.handle});
    }

    void SceneLooks::Cube(Fenrir::Entity& entity, const Vec3& colour)
    {
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{m_cube, Material(colour)});
    }

    void SceneLooks::Sphere(Fenrir::Entity& entity, const Vec3& colour)
    {
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{m_sphere, Material(colour)});
    }

    Fenrir::AssetHandle<Fenrir::Material> SceneLooks::Material(const Vec3& colour)
    {
        for (const auto& [made, material] : m_materials)
        {
            if (made == colour)
            {
                return material;
            }
        }
        const Fenrir::AssetHandle<Fenrir::Material> material = m_store->CreateMaterial(m_lit, *m_scene);
        m_store->SetColour(
            material, "baseColour",
            Fenrir::Colour{SrgbToLinear(colour.x), SrgbToLinear(colour.y), SrgbToLinear(colour.z), 1.0f});
        m_materials.emplace_back(colour, material);
        return material;
    }

    Fenrir::Math::Quat LookRotation(float yawDegrees, float pitchDegrees)
    {
        // A camera entity looks along its -Z: a turn of s about +Y looks along (-sin s, 0, -cos s), which is the
        // legacy yaw's (cos yaw, 0, sin yaw) when s = -(yaw + 90 degrees).
        constexpr float radians = 3.14159265f / 180.0f;
        return Fenrir::Math::AngleAxis(-(yawDegrees + 90.0f) * radians, Vec3(0.0f, 1.0f, 0.0f)) *
               Fenrir::Math::AngleAxis(pitchDegrees * radians, Vec3(1.0f, 0.0f, 0.0f));
    }

    void ShowReadouts(Fenrir::Scene& scene, std::span<const Fenrir::Entity> entities)
    {
        Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
        if (draw == nullptr)
        {
            return;
        }
        constexpr float margin = 16.0f;
        constexpr float lineHeight = 20.0f;
        float y = margin;
        for (const Fenrir::Entity& entity : entities)
        {
            if (!entity.IsValid())
            {
                continue;
            }
            draw->ScreenText({margin, y}, entity.GetComponent<Fenrir::Name>().Get(), Fenrir::Colour::White);
            y += lineHeight;
        }
    }

    void AddDebugCategoryCycling(Fenrir::App& app)
    {
        app.AddSequentialSystem(
            Fenrir::SchedulePriority::Update, [mode = std::size_t{0}](Fenrir::App& running) mutable {
                const auto& keyboard = running.GetResource<Fenrir::Keyboard>();
                const bool stepped = keyboard.JustPressed(Fenrir::Key::F2);
                if (stepped)
                {
                    mode = (mode + 1) % DebugModes.size();
                    running.Logger()->Info(std::string("Physics debug lines: ") + DebugModes.at(mode).name);
                }

                Fenrir::Scene& scene = running.GetActiveScene();
                Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(scene);
                if (world == nullptr)
                {
                    return;
                }
                // Each scene's world keeps its own categories; one entered since the last F2 gets the app's once.
                if (stepped || scene.GetResource<DebugCategoriesApplied>() == nullptr)
                {
                    world->SetDebugDraw(DebugModes.at(mode).categories);
                    if (scene.GetResource<DebugCategoriesApplied>() == nullptr)
                    {
                        scene.AddResource<DebugCategoriesApplied>();
                    }
                }
            });
    }
} // namespace PhysicsDemo
