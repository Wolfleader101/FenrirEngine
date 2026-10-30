#include "Showcase.hpp"

#include <cstdint>
#include <format>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirScripting/Generated/FenrirScriptingMappings.hpp"

// How these look to C#. The C# structs (scripts/Bobbing.cs, scripts/Controls.cs) must match field for field, or the
// script assembly refuses to load and says which field is wrong.

template <>
struct Iris::StructMapping<ScriptingDemo::Bob>
{
    static constexpr std::string_view typeName = "ScriptingDemo.Scripts.Bob";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(ScriptingDemo::Bob, Amplitude), IRIS_FIELD(ScriptingDemo::Bob, Speed),
                           IRIS_FIELD(ScriptingDemo::Bob, Phase), IRIS_FIELD(ScriptingDemo::Bob, BaseY));
};

template <>
struct Iris::StructMapping<ScriptingDemo::BurstEvent>
{
    static constexpr std::string_view typeName = "ScriptingDemo.Scripts.Burst";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(ScriptingDemo::BurstEvent, Count));
};

namespace ScriptingDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        /** @brief Makes an entity bob. The C# Bobber system does the moving. */
        void AttachBob(Fenrir::Entity entity, float baseY)
        {
            Bob bob;
            bob.Amplitude = 0.75f;
            bob.Speed = 2.0f;
            bob.BaseY = baseY;
            entity.AddComponent<Bob>(bob);
        }

        // As the legacy DemoApp's: a parent "Backpacks" holding "Backpack 1" at the origin and "Backpack 2" five units
        // above it, which bobs. C# finds them by name (scripts/Spin.cs) and spins them.
        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const auto backpack = store.Load<Fenrir::Model>("backpack/backpack.obj", scene);
            if (!backpack.Ok())
            {
                app.Logger()->Error("Showcase: the backpack did not load from the asset root");
                return;
            }

            Fenrir::EntityList& entities = scene.GetEntityList();
            Fenrir::Entity backpacks = entities.CreateEntity();
            backpacks.GetComponent<Fenrir::Name>().Set("Backpacks");

            Fenrir::Entity backpack1 = Fenrir::SpawnModel(app, scene, backpack.handle, backpacks);
            backpack1.GetComponent<Fenrir::Name>().Set("Backpack 1");

            Fenrir::Entity backpack2 = Fenrir::SpawnModel(app, scene, backpack.handle, backpacks);
            backpack2.GetComponent<Fenrir::Name>().Set("Backpack 2");
            constexpr float backpack2Y = 5.0f;
            backpack2.GetComponent<Fenrir::Transform>().pos = Vec3(0.0f, backpack2Y, 0.0f);
            // Backpack 2 bobs up and down: C++ sets how, a C# system does it (scripts/Bobbing.cs).
            AttachBob(backpack2, backpack2Y);

            // The legacy demo's directional light, which shone along (-0.2, -1, -0.3); this one shines along its -Z.
            Fenrir::Entity light = entities.CreateEntity();
            light.GetComponent<Fenrir::Name>().Set("Light");
            light.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.6f, Vec3(0.0f, 1.0f, 0.0f)) *
                                                          Fenrir::Math::AngleAxis(-1.1f, Vec3(1.0f, 0.0f, 0.0f));
            light.AddComponent<Fenrir::DirectionalLight>();
        }

        /**
         * @brief Reads what C# wrote (Bob::Phase) and shows it in the entity's name. This is the shared component
         * working in the other direction: C# data, read by C++ with no copying or marshalling.
         */
        void ShowBobPhase(Fenrir::App& app)
        {
            static bool saidSo = false;
            app.GetActiveScene().GetEntityList().ForEach<Fenrir::Name, Bob>([&app](Fenrir::Name& name, Bob& bob) {
                if (!saidSo && bob.Phase > 0.0f)
                {
                    saidSo = true;
                    app.Logger()->Info("C++ read Bob.Phase = {:.2f}, which C# wrote into the same component.",
                                       bob.Phase);
                }
                name.Set(std::format("Backpack 2 (bob phase {:.2f})", bob.Phase).c_str());
            });
        }

        /** @brief Logs every burst C# sends, once each: the system's own reader remembers which it has seen. */
        Fenrir::SystemFunc LogBursts()
        {
            return [bursts = Fenrir::EventReader<BurstEvent>{}](Fenrir::App& app) mutable {
                for (const BurstEvent& burst : bursts.Read(app))
                {
                    app.Logger()->Info("C++ saw a burst of {} sparks sent from C#.", burst.Count);
                }
            };
        }

        /**
         * @brief The legacy editor's hierarchy, where the showcase's changing names were seen: each backpack's and
         * spark's Name in world text above it, this frame.
         */
        void ShowNames(Fenrir::App& app)
        {
            Fenrir::Scene& scene = app.GetActiveScene();
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (draw == nullptr)
            {
                return;
            }
            Fenrir::EntityList& entities = scene.GetEntityList();
            entities.ForEach<Fenrir::Name>([&](auto handle, Fenrir::Name& name) {
                const std::string_view text = name.Get();
                const bool backpack = text.starts_with("Backpack ");
                if (!backpack && !text.starts_with("Spark "))
                {
                    return;
                }
                Fenrir::TextOptions options;
                options.relativeTo = entities.GetEntity(static_cast<std::uint32_t>(handle));
                draw->WorldText(Vec3(0.0f, backpack ? 1.6f : 0.0f, 0.0f), text, Fenrir::Colour::White, options);
            });
        }
    } // namespace

    Fenrir::ScriptingExtension Extension()
    {
        Fenrir::ScriptingExtension extension;
        extension.name = "ScriptingDemo";
        extension.components = {Fenrir::MakeComponent<Bob>("ScriptingDemo.Bob")};
        extension.events = {Fenrir::MakeEvent<BurstEvent>("ScriptingDemo.Burst")};
        return extension;
    }

    FenrirDemo::DemoScene Showcase()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Showcase";
        scene.keyHelp = "R: reverse the backpacks' spin. Space: a burst of sparks.";
        scene.keys = {Fenrir::Key::R, Fenrir::Key::Space};
        scene.camera.position = {0.0f, 4.0f, 16.0f};
        scene.spawn = Spawn;
        return scene;
    }

    void AddSystems(Fenrir::App& app)
    {
        app.AddSequentialSystems(Fenrir::SchedulePriority::Update, {ShowBobPhase, LogBursts(), ShowNames});
    }
} // namespace ScriptingDemo
