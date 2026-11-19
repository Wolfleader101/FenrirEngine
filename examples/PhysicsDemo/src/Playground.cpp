// Playground (C++), the legacy DemoApp's Default scene's physics demo: a static ground, a stack of dynamic boxes and a
// kinematic paddle that game code sweeps back and forth in Tick, pushing the boxes. Each body is the unit cube in its
// own flat colour, so they are easy to tell apart. Beside them, a static textured box (Khronos's BoxTextured) turned on
// its edge, whose collider is a triangle mesh made from the model's vertices, with boxes dropped onto it. F2 cycles the
// physics debug lines.
//
// Playground (C#), its twin in the scene list, is the legacy Physics (C#) scene: C++ gives it only its entry here, and
// scripts/Playground.cs builds and runs it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "PhysicsScenes.hpp"
#include "SceneLooks.hpp"

namespace PhysicsDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Playground (C++)";

        // Not constexpr: MSVC does not take glm's vec3 constructor as one.
        const std::array<Vec3, 8> BoxColours = {
            Vec3(0.90f, 0.20f, 0.20f), // red
            Vec3(0.20f, 0.75f, 0.25f), // green
            Vec3(0.20f, 0.40f, 0.95f), // blue
            Vec3(0.95f, 0.90f, 0.20f), // yellow
            Vec3(0.70f, 0.25f, 0.85f), // purple
            Vec3(0.20f, 0.85f, 0.85f), // cyan
            Vec3(0.95f, 0.45f, 0.70f), // pink
            Vec3(0.95f, 0.95f, 0.95f), // white
        };

        /** @brief A model's vertices and triangles in its space. */
        struct ModelShape
        {
            std::vector<Vec3> vertices;
            std::vector<std::uint32_t> indices;
        };

        // Every mesh of every node, placed by the nodes' rest transforms: FenrirPhysics takes plain vertices and
        // indices, so it never sees the model.
        ModelShape ShapeOf(const Fenrir::AssetStore& store, const Fenrir::Model& model)
        {
            ModelShape shape;
            std::vector<Fenrir::Transform> placed;
            placed.reserve(model.nodes.size());
            for (const Fenrir::ModelNode& node : model.nodes)
            {
                placed.push_back(node.parent < 0 ? node.rest
                                                 : Fenrir::ComposeTransform(
                                                       placed.at(static_cast<std::size_t>(node.parent)), node.rest));
                const Fenrir::Transform& at = placed.back();
                for (const Fenrir::ModelMesh& part : node.meshes)
                {
                    const Fenrir::MeshData* mesh = store.TryGet(part.mesh);
                    if (mesh == nullptr)
                    {
                        continue;
                    }
                    const auto first = static_cast<std::uint32_t>(shape.vertices.size());
                    for (const Vec3& position : mesh->positions)
                    {
                        shape.vertices.push_back(at.pos + at.rot * (at.scale * position));
                    }
                    for (const std::uint32_t index : mesh->indices)
                    {
                        shape.indices.push_back(first + index);
                    }
                }
            }
            return shape;
        }

        // Draws every MeshRenderer at or below `entity` with `material`.
        void Recolour(const Fenrir::Entity& entity, Fenrir::AssetHandle<Fenrir::Material> material)
        {
            if (entity.HasComponent<Fenrir::MeshRenderer>())
            {
                entity.GetComponent<Fenrir::MeshRenderer>().material = material;
            }
            entity.ForEachChild([&](const Fenrir::Entity& child) { Recolour(child, material); });
        }

        class PlaygroundScene
        {
          public:
            explicit PlaygroundScene(Fenrir::Physics& physics) : m_physics(&physics)
            {
            }

            void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
            {
                m_time = 0.0;
                SceneLooks looks(app, scene);
                Fenrir::EntityList& entityList = scene.GetEntityList();
                const Fenrir::Math::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);

                Fenrir::Entity physicsRoot = entityList.CreateEntity();
                physicsRoot.GetComponent<Fenrir::Name>().Set("Physics");

                // A collider with no RigidBody: static. Grey, its top face at y = -3.75.
                Fenrir::Entity ground = entityList.CreateEntity();
                ground.GetComponent<Fenrir::Name>().Set("Ground");
                ground.GetComponent<Fenrir::Transform>() =
                    Fenrir::Transform(Vec3(0.0f, -4.0f, -12.0f), identity, Vec3(16.0f, 0.5f, 16.0f));
                ground.AddComponent<Fenrir::Collider>();
                looks.Cube(ground, Vec3(0.45f, 0.45f, 0.45f));
                physicsRoot.AddChild(ground);

                for (std::size_t i = 0; i < BoxColours.size(); ++i)
                {
                    const float step = static_cast<float>(i);
                    Fenrir::Entity box = entityList.CreateEntity();
                    box.GetComponent<Fenrir::Name>().Set(("Box " + std::to_string(i)).c_str());
                    box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(
                        Vec3(0.3f * static_cast<float>(i % 3) - 0.3f, 1.5f * step, -12.0f),
                        Fenrir::Math::AngleAxis(0.3f * step, Fenrir::Math::Normalized(Vec3(1.0f, 1.0f, 0.0f))),
                        Vec3(1.0f));
                    box.AddComponent<Fenrir::RigidBody>();
                    box.AddComponent<Fenrir::Collider>();
                    looks.Cube(box, BoxColours.at(i));
                    physicsRoot.AddChild(box);
                }

                // Kinematic: moved by MovePaddle in Tick, pushes the boxes and is not pushed. Orange.
                m_paddle = entityList.CreateEntity();
                m_paddle.GetComponent<Fenrir::Name>().Set("Paddle");
                m_paddle.GetComponent<Fenrir::Transform>() =
                    Fenrir::Transform(Vec3(-6.0f, -3.0f, -12.0f), identity, Vec3(1.0f, 1.0f, 4.0f));
                m_paddle.AddComponent<Fenrir::RigidBody>(
                    Fenrir::RigidBody{.motionType = Fenrir::MotionType::Kinematic});
                m_paddle.AddComponent<Fenrir::Collider>();
                looks.Cube(m_paddle, Vec3(1.0f, 0.55f, 0.0f));
                physicsRoot.AddChild(m_paddle);

                AddMeshCollider(app, scene, looks, physicsRoot);
            }

            // Game code in Tick: sweeps the paddle from x = -6 to 6 and back, once every 12.6 seconds.
            void MovePaddle(Fenrir::App& app)
            {
                if (app.GetActiveScene().GetName() != SceneName || !m_paddle.IsValid())
                {
                    return;
                }
                m_time += app.GetTime().tickRate;
                m_paddle.GetComponent<Fenrir::Transform>().pos.x = static_cast<float>(-6.0 * std::cos(m_time * 0.5));
            }

          private:
            // A static textured box, its collider a triangle mesh cooked from the model's vertex data (the entity's
            // scale is baked into it). Brown, standing on one edge on the ground in front of the stack, with three
            // boxes dropped onto it that land on its sloping faces and slide off them, never resting on top of its
            // bounding box.
            void AddMeshCollider(Fenrir::App& app, Fenrir::Scene& scene, SceneLooks& looks, Fenrir::Entity& physicsRoot)
            {
                Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
                const auto model = store.Load<Fenrir::Model>("BoxTextured/BoxTextured.gltf", scene);
                const Fenrir::Model* data = model.Ok() ? store.TryGet(model.handle) : nullptr;
                if (data == nullptr)
                {
                    app.Logger()->Error("Playground (C++): the textured box did not load from the asset root");
                    return;
                }
                const ModelShape shape = ShapeOf(store, *data);
                const Fenrir::CollisionMesh collisionMesh =
                    m_physics->CreateCollisionMesh(shape.vertices, shape.indices);

                // Turned 45 degrees about Z, so it stands on an edge with a ridge on top, and scaled up.
                const Fenrir::Math::Quat onEdge = Fenrir::Math::AngleAxis(0.785398f, Vec3(0.0f, 0.0f, 1.0f));
                const Vec3 scale(2.5f);
                float lowest = std::numeric_limits<float>::max();
                float highest = std::numeric_limits<float>::lowest();
                for (const Vec3& vertex : shape.vertices)
                {
                    const float y = (onEdge * (scale * vertex)).y;
                    lowest = std::min(lowest, y);
                    highest = std::max(highest, y);
                }

                Fenrir::EntityList& entityList = scene.GetEntityList();
                const Fenrir::Math::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
                const Vec3 base(5.0f, -3.75f - lowest, -7.0f);
                Fenrir::Entity ridge = Fenrir::SpawnModel(app, scene, model.handle);
                ridge.GetComponent<Fenrir::Name>().Set("Mesh collider (textured box)");
                ridge.GetComponent<Fenrir::Transform>() = Fenrir::Transform(base, onEdge, scale);
                ridge.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::MeshShape{collisionMesh}});
                Recolour(ridge, looks.Material(Vec3(0.55f, 0.35f, 0.2f)));
                physicsRoot.AddChild(ridge);

                const float top = base.y + highest;
                for (int i = 0; i < 3; ++i)
                {
                    const float step = static_cast<float>(i);
                    Fenrir::Entity box = entityList.CreateEntity();
                    box.GetComponent<Fenrir::Name>().Set(("Ridge box " + std::to_string(i)).c_str());
                    box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(
                        base + Vec3(0.6f * step - 0.6f, top - base.y + 2.0f + 1.5f * step, 0.0f), identity, Vec3(0.5f));
                    box.AddComponent<Fenrir::RigidBody>();
                    box.AddComponent<Fenrir::Collider>();
                    looks.Cube(box, BoxColours.at(static_cast<std::size_t>(i)));
                    physicsRoot.AddChild(box);
                }
            }

            Fenrir::Physics* m_physics;
            Fenrir::Entity m_paddle;
            double m_time = 0.0;
        };
    } // namespace

    FenrirDemo::DemoScene Playground(Fenrir::App& app, Fenrir::Physics& physics)
    {
        auto playground = std::make_shared<PlaygroundScene>(physics);
        app.AddSequentialSystem(Fenrir::SchedulePriority::Tick,
                                [playground](Fenrir::App& running) { playground->MovePaddle(running); });

        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        // No keys of its own: the paddle sweeps the stack, and F2 (the app's) steps the debug lines.
        scene.tutorial = FenrirDemo::DemoTutorial{
            .title = "PhysicsDemo",
            .steps = {
                {.title = "The scenes",
                 .text = "Physics scenes, each C++ one followed by its C# twin. Here a paddle sweeps a stack of boxes. "
                         "Step through the scenes with PageDown and PageUp, or click one in the Scenes section.",
                 .keys = {{"PageDown/PageUp", "next/previous scene"}}},
                {.title = "The demo UI",
                 .text = "The panel on the right holds the scenes, the readings and the switches, and the help at the "
                         "bottom left every key. F1 hides all of it, for a clear view, and shows it again.",
                 .keys = {{"F1", "hide or show the demo UI kit"}}},
                {.title = "Debug lines",
                 .text = "F2 steps physics's debug lines: colliders, contacts, velocities and the rest. The Physics "
                         "section's Debug lines row picks them too. Press F2.",
                 .keys = {{"F2", "step the physics debug lines"}},
                 .done = [](Fenrir::App& app) { return AnyJustPressed(app, {Fenrir::Key::F2}); }},
                {.title = "The free-fly camera",
                 .text = "Hold the right mouse button to look round and WASD to fly; Shift goes faster and the wheel "
                         "changes the speed. A scene with a camera of its own starts with the free-fly camera off: "
                         "F3 switches between them.",
                 .keys = {{"Right mouse + WASD", "look and fly"}, {"F3", "free-fly camera on/off"}}}}};
        // F2 round every category and back to off.
        scene.keys.assign(9, Fenrir::Key::F2);
        scene.camera.position = {0.0f, 1.0f, 6.0f};
        scene.camera.pitch = -0.2f;
        scene.spawn = [playground](Fenrir::App& running, Fenrir::Scene& spawned) {
            playground->Spawn(running, spawned);
        };
        return scene;
    }

    FenrirDemo::DemoScene PlaygroundCSharp()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Playground (C#)";
        scene.keyRows = {{"J", "kick the bodies into the air"},
                         {"Left click", "cast a ray: log what it hit and poke it"}};
        scene.keys = {Fenrir::Key::J};
        scene.camera.position = {0.0f, 1.0f, 6.0f};
        scene.camera.pitch = -0.2f;
        scene.csharpScene = PlaygroundCSharpScene;
        return scene;
    }
} // namespace PhysicsDemo
