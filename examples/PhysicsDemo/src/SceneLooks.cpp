#include "SceneLooks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDemoScaffold/DemoUI.hpp"
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

        /** @brief A scene's "Readings" section and its readouts by label, made by ShowReadouts. */
        struct SceneReadouts
        {
            FenrirDemo::DemoSection section;
            std::vector<std::pair<std::string, FenrirDemo::DemoReadout>> rows;
        };

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
        // The skybox is drawn behind the scene and lights it (ADR-0047).
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

    bool AnyJustPressed(Fenrir::App& app, std::initializer_list<Fenrir::Key> keys)
    {
        const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
        return std::ranges::any_of(keys, [&keyboard](Fenrir::Key key) { return keyboard.JustPressed(key); });
    }

    std::string_view ReadingValue(std::string_view name, std::string_view label)
    {
        if (label.empty() || !name.starts_with(label))
        {
            return name;
        }
        const std::string_view rest = name.substr(label.size());
        if (rest.starts_with(": "))
        {
            return rest.substr(2);
        }
        if (rest.starts_with(' '))
        {
            return rest.substr(1);
        }
        return name;
    }

    void ShowReadouts(Fenrir::App& app, std::span<const Reading> readings)
    {
        auto* kit = app.TryGetResource<FenrirDemo::DemoUI>();
        if (kit == nullptr)
        {
            return;
        }
        Fenrir::Scene& scene = app.GetActiveScene();
        auto* shown = scene.GetResource<SceneReadouts>();
        if (shown == nullptr)
        {
            shown = &scene.AddResource<SceneReadouts>(SceneReadouts{.section = kit->SceneSection("Readings")});
        }
        for (const Reading& reading : readings)
        {
            auto row = std::ranges::find(shown->rows, reading.label,
                                         [](const auto& made) -> const std::string& { return made.first; });
            if (row == shown->rows.end())
            {
                shown->rows.emplace_back(std::string(reading.label),
                                         shown->section.Readout(std::string(reading.label)));
                row = std::prev(shown->rows.end());
            }
            const bool valid = reading.entity.IsValid();
            row->second.SetShown(valid);
            if (valid)
            {
                row->second.Set(ReadingValue(reading.entity.GetComponent<Fenrir::Name>().Get(), reading.label));
            }
        }
    }

    void AddDebugCategoryCycling(Fenrir::App& app)
    {
        // The app's mode, which F2 and the Debug lines choice share, and whether either changed it since the system
        // last ran.
        struct DebugLines
        {
            int mode = 0;
            bool changed = false;
        };
        auto lines = std::make_shared<DebugLines>();

        std::vector<std::string> names;
        for (const DebugMode& mode : DebugModes)
        {
            names.emplace_back(mode.name);
        }
        // Without the kit (FenrirUI did not start), F2 alone steps them.
        if (auto* kit = app.TryGetResource<FenrirDemo::DemoUI>())
        {
            kit->AppSection("Physics").Choice("Debug lines",
                                              FenrirDemo::DemoValue<int>([lines] { return lines->mode; },
                                                                         [lines](const int& mode) {
                                                                             if (mode != lines->mode)
                                                                             {
                                                                                 lines->mode = mode;
                                                                                 lines->changed = true;
                                                                             }
                                                                         }),
                                              std::move(names), "Which physics debug lines are drawn; F2 steps them");
        }

        app.AddSequentialSystem(Fenrir::SchedulePriority::Update, [lines](Fenrir::App& running) {
            const auto& keyboard = running.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::F2))
            {
                lines->mode = (lines->mode + 1) % static_cast<int>(DebugModes.size());
                lines->changed = true;
            }
            const bool stepped = lines->changed;
            const DebugMode& mode = DebugModes.at(static_cast<std::size_t>(lines->mode));
            if (stepped)
            {
                lines->changed = false;
                running.Logger()->Info(std::string("Physics debug lines: ") + mode.name);
            }

            Fenrir::Scene& scene = running.GetActiveScene();
            Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(scene);
            if (world == nullptr)
            {
                return;
            }
            // Each scene's world keeps its own categories; one entered since the last change gets the app's once.
            if (stepped || scene.GetResource<DebugCategoriesApplied>() == nullptr)
            {
                world->SetDebugDraw(mode.categories);
                if (scene.GetResource<DebugCategoriesApplied>() == nullptr)
                {
                    scene.AddResource<DebugCategoriesApplied>();
                }
            }
        });
    }
} // namespace PhysicsDemo
