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

        // As the legacy DemoApp's, with Khronos's BoxTextured for its backpack: a parent "Boxes" holding "Box 1" at
        // the origin and "Box 2" five units above it, which bobs. C# finds them by name (scripts/Spin.cs) and spins
        // them.
        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const auto model = store.Load<Fenrir::Model>("BoxTextured/BoxTextured.gltf", scene);
            if (!model.Ok())
            {
                app.Logger()->Error("Showcase: the textured box did not load from the asset root");
                return;
            }

            Fenrir::EntityList& entities = scene.GetEntityList();
            Fenrir::Entity boxes = entities.CreateEntity();
            boxes.GetComponent<Fenrir::Name>().Set("Boxes");

            Fenrir::Entity box1 = Fenrir::SpawnModel(app, scene, model.handle, boxes);
            box1.GetComponent<Fenrir::Name>().Set("Box 1");

            Fenrir::Entity box2 = Fenrir::SpawnModel(app, scene, model.handle, boxes);
            box2.GetComponent<Fenrir::Name>().Set("Box 2");
            constexpr float box2Y = 5.0f;
            box2.GetComponent<Fenrir::Transform>().pos = Vec3(0.0f, box2Y, 0.0f);
            // Box 2 bobs up and down: C++ sets how, a C# system does it (scripts/Bobbing.cs).
            AttachBob(box2, box2Y);

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
                name.Set(std::format("Box 2 (bob phase {:.2f})", bob.Phase).c_str());
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
         * @brief The legacy editor's hierarchy, where the showcase's changing names were seen: each box's and
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
                const bool box = text.starts_with("Box ");
                if (!box && !text.starts_with("Spark "))
                {
                    return;
                }
                Fenrir::TextOptions options;
                options.relativeTo = entities.GetEntity(static_cast<std::uint32_t>(handle));
                draw->WorldText(Vec3(0.0f, box ? 1.0f : 0.0f, 0.0f), text, Fenrir::Colour::White, options);
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
        scene.keyHelp = "R: reverse the boxes' spin. Space: a burst of sparks.";
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
