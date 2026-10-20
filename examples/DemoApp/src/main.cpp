#include <string>
#include <unordered_set>

// clang-format off
#include "Window.hpp"
// clang-format on

#include "CameraController.hpp"
#include "ModelLibrary.hpp"
#include "ShaderLibrary.hpp"
#include "TextureLibrary.hpp"

#include "FenrirCamera/Camera.hpp"
#include "FenrirMath/Math.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"

#include <iostream>

#include "DemoCamera.hpp"
#include "DemoMesh.hpp"
#include "DemoScenes.hpp"
#include "Editor.hpp"
#include "GLRenderer.hpp"
#include "Material.hpp"

#include <glaze/glaze.hpp>

#ifdef FENRIR_SCRIPTING
#include "FenrirScripting/Scripting.hpp"
#include "ScriptingDemo.hpp"
#endif

#ifdef FENRIR_PHYSICS
#include <array>
#include <cmath>

#include "CharacterScene.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "JointsScene.hpp"
#include "VehicleScene.hpp"
#endif

#if defined(FENRIR_PHYSICS) && defined(FENRIR_SCRIPTING)
#include "CSharpScenes.hpp"
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"
#endif

template <>
struct glz::meta<ProjectSettings>
{
    using T = ProjectSettings;
    static constexpr auto value =
        object(&T::identity, &T::name, &T::version, &T::description, &T::author, &T::assetPath, &T::scriptAssembly);
};

// template <>
// struct glz::meta<Fenrir::Math::Vec3>
// {
//     using T = Fenrir::Math::Vec3;
//     static constexpr auto value = object(&T::x, &T::y, &T::z);
// };

// template <>
// struct glz::meta<Fenrir::Math::Quat>
// {
//     using T = Fenrir::Math::Quat;
//     static constexpr auto value = object(&T::x, &T::y, &T::z, &T::w);
// };
// template <>
// struct glz::meta<Fenrir::Transform>
// {
//     using T = Fenrir::Transform;
//     static constexpr auto value = object(&T::pos, &T::rot, &T::scale);
// };

// template <>
// struct glz::meta<Fenrir::Entity>
// {
//     using T = Fenrir::Entity;
//     static constexpr auto value = object("entity", &T::GetId);
// };

struct DirLight
{
    Fenrir::Math::Vec3 direction = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);

    Fenrir::Math::Vec3 ambient = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
    Fenrir::Math::Vec3 diffuse = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
    Fenrir::Math::Vec3 specular = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
};

struct PointLight
{
    Fenrir::Math::Vec3 ambient = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
    Fenrir::Math::Vec3 diffuse = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
    Fenrir::Math::Vec3 specular = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);

    float constant;
    float linear;
    float quadratic;
};

struct SpotLight
{
    Fenrir::Math::Vec3 direction = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);

    Fenrir::Math::Vec3 ambient = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
    Fenrir::Math::Vec3 diffuse = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
    Fenrir::Math::Vec3 specular = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);

    float constant;
    float linear;
    float quadratic;

    float cutOff;
    float outerCutOff;
};

static Shader myShader;
static Shader lightShader;
static Shader aabbShader;
static Shader skyboxShader;
static Shader shadedShader;

static Skybox skybox;

static Model backpack;
static Model cube;
static Model sphere;

static const glm::vec3 pointLightPositions[4] = {glm::vec3(0.7f, 0.2f, 2.0f), glm::vec3(2.3f, -3.3f, -4.0f),
                                                 glm::vec3(-4.0f, 2.0f, -12.0f), glm::vec3(0.0f, 0.0f, -3.0f)};

static void InitLights(Fenrir::App& app)
{

    Fenrir::EntityList& entityList = app.GetActiveScene().GetEntityList();

    Fenrir::Entity light_parent = entityList.CreateEntity();
    light_parent.GetComponent<Fenrir::Name>().Set("Lights");

    Fenrir::Entity light1_ent = entityList.CreateEntity();
    Fenrir::Entity light2_ent = entityList.CreateEntity();
    Fenrir::Entity light3_ent = entityList.CreateEntity();
    Fenrir::Entity light4_ent = entityList.CreateEntity();

    light1_ent.GetComponent<Fenrir::Name>().Set("Light 1");
    light2_ent.GetComponent<Fenrir::Name>().Set("Light 2");
    light3_ent.GetComponent<Fenrir::Name>().Set("Light 3");
    light4_ent.GetComponent<Fenrir::Name>().Set("Light 4");

    light_parent.AddChild(light1_ent);
    light_parent.AddChild(light2_ent);
    light_parent.AddChild(light3_ent);
    light_parent.AddChild(light4_ent);

    Fenrir::Transform light1_trans = {Fenrir::Math::Vec3(0.7f, 0.2f, 2.0f), Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                      Fenrir::Math::Vec3(0.2f, 0.2f, 0.2f)};

    Fenrir::Transform light2_trans = {Fenrir::Math::Vec3(2.3f, -3.3f, -4.0f),
                                      Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), Fenrir::Math::Vec3(0.2f, 0.2f, 0.2f)};
    Fenrir::Transform light3_trans = {Fenrir::Math::Vec3(-4.0f, 2.0f, -12.0f),
                                      Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), Fenrir::Math::Vec3(0.2f, 0.2f, 0.2f)};

    Fenrir::Transform light4_trans = {Fenrir::Math::Vec3(0.0f, 0.0f, -3.0f), Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                      Fenrir::Math::Vec3(0.2f, 0.2f, 0.2f)};

    light1_ent.GetComponent<Fenrir::Transform>() = light1_trans;
    light2_ent.GetComponent<Fenrir::Transform>() = light2_trans;
    light3_ent.GetComponent<Fenrir::Transform>() = light3_trans;
    light4_ent.GetComponent<Fenrir::Transform>() = light4_trans;

    light1_ent.AddComponent<Material>(Material{lightShader});
    light2_ent.AddComponent<Material>(Material{lightShader});
    light3_ent.AddComponent<Material>(Material{lightShader});
    light4_ent.AddComponent<Material>(Material{lightShader});

    light1_ent.AddComponent<Model>(cube);
    light2_ent.AddComponent<Model>(cube);
    light3_ent.AddComponent<Model>(cube);
    light4_ent.AddComponent<Model>(cube);
}

static void InitBackpacks(Fenrir::App& app)
{
    Fenrir::EntityList& entityList = app.GetActiveScene().GetEntityList();

    Fenrir::Entity backpack_parent = entityList.CreateEntity();

    backpack_parent.GetComponent<Fenrir::Name>().Set("Backpacks");

    Fenrir::Entity backpack1_ent = entityList.CreateEntity();
    Fenrir::Entity backpack2_ent = entityList.CreateEntity();

    backpack1_ent.GetComponent<Fenrir::Name>().Set("Backpack 1");
    backpack2_ent.GetComponent<Fenrir::Name>().Set("Backpack 2");

    backpack_parent.AddChild(backpack1_ent);
    backpack_parent.AddChild(backpack2_ent);

    Fenrir::Transform backpackTransform(Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f),
                                        Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                        Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f));

    Fenrir::Transform backpack2Transform(Fenrir::Math::Vec3(0.0f, 5.0f, 0.0f),
                                         Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                         Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f));

    backpack1_ent.GetComponent<Fenrir::Transform>() = backpackTransform;

    backpack2_ent.GetComponent<Fenrir::Transform>() = backpack2Transform;

#ifdef FENRIR_SCRIPTING
    // Backpack 2 bobs up and down: C++ sets how, a C# system does it (see ScriptingDemo.hpp and scripts/Bobbing.cs).
    ScriptingDemo::AttachBob(backpack2_ent, backpack2Transform.pos.y);
#endif

    Material backpackMaterial = {myShader};

    // directional light
    backpackMaterial.properties["dirLight.direction"] = Fenrir::Math::Vec3(-0.2f, -1.0f, -0.3f);
    backpackMaterial.properties["dirLight.ambient"] = Fenrir::Math::Vec3(0.05f, 0.05f, 0.05f);
    backpackMaterial.properties["dirLight.diffuse"] = Fenrir::Math::Vec3(0.4f, 0.4f, 0.4f);
    backpackMaterial.properties["dirLight.specular"] = Fenrir::Math::Vec3(0.5f, 0.5f, 0.5f);

    // point light 1
    backpackMaterial.properties["pointLights[0].pos"] = pointLightPositions[0];
    backpackMaterial.properties["pointLights[0].ambient"] = Fenrir::Math::Vec3(0.05f, 0.05f, 0.05f);
    backpackMaterial.properties["pointLights[0].diffuse"] = Fenrir::Math::Vec3(0.8f, 0.8f, 0.8f);
    backpackMaterial.properties["pointLights[0].specular"] = Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f);
    backpackMaterial.properties["pointLights[0].constant"] = 1.0f;
    backpackMaterial.properties["pointLights[0].linear"] = 0.09f;
    backpackMaterial.properties["pointLights[0].quadratic"] = 0.032f;

    // point light 2
    backpackMaterial.properties["pointLights[1].pos"] = pointLightPositions[1];
    backpackMaterial.properties["pointLights[1].ambient"] = Fenrir::Math::Vec3(0.05f, 0.05f, 0.05f);
    backpackMaterial.properties["pointLights[1].diffuse"] = Fenrir::Math::Vec3(0.8f, 0.8f, 0.8f);
    backpackMaterial.properties["pointLights[1].specular"] = Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f);

    backpackMaterial.properties["pointLights[1].constant"] = 1.0f;
    backpackMaterial.properties["pointLights[1].linear"] = 0.09f;
    backpackMaterial.properties["pointLights[1].quadratic"] = 0.032f;

    // point light 3
    backpackMaterial.properties["pointLights[2].pos"] = pointLightPositions[2];
    backpackMaterial.properties["pointLights[2].ambient"] = Fenrir::Math::Vec3(0.05f, 0.05f, 0.05f);
    backpackMaterial.properties["pointLights[2].diffuse"] = Fenrir::Math::Vec3(0.8f, 0.8f, 0.8f);
    backpackMaterial.properties["pointLights[2].specular"] = Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f);

    backpackMaterial.properties["pointLights[2].constant"] = 1.0f;
    backpackMaterial.properties["pointLights[2].linear"] = 0.09f;
    backpackMaterial.properties["pointLights[2].quadratic"] = 0.032f;

    // point light 4
    backpackMaterial.properties["pointLights[3].pos"] = pointLightPositions[3];
    backpackMaterial.properties["pointLights[3].ambient"] = Fenrir::Math::Vec3(0.05f, 0.05f, 0.05f);
    backpackMaterial.properties["pointLights[3].diffuse"] = Fenrir::Math::Vec3(0.8f, 0.8f, 0.8f);
    backpackMaterial.properties["pointLights[3].specular"] = Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f);

    backpackMaterial.properties["pointLights[3].constant"] = 1.0f;
    backpackMaterial.properties["pointLights[3].linear"] = 0.09f;
    backpackMaterial.properties["pointLights[3].quadratic"] = 0.032f;

    // spotLight
    backpackMaterial.properties["spotLight.ambient"] = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
    backpackMaterial.properties["spotLight.diffuse"] = Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f);
    backpackMaterial.properties["spotLight.specular"] = Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f);

    backpackMaterial.properties["spotLight.constant"] = 1.0f;
    backpackMaterial.properties["spotLight.linear"] = 0.09f;
    backpackMaterial.properties["spotLight.quadratic"] = 0.032f;
    backpackMaterial.properties["spotLight.cutOff"] = glm::cos(glm::radians(12.5f));
    backpackMaterial.properties["spotLight.outerCutOff"] = glm::cos(glm::radians(15.0f));

    // set material in shader (diffuse and specular is set as texture once above)
    backpackMaterial.properties["material.shininess"] = 32.0f; // bind diffuse map

    backpack1_ent.AddComponent<Material>(backpackMaterial);
    backpack2_ent.AddComponent<Material>(backpackMaterial);

    backpack1_ent.AddComponent<Model>(backpack);
    backpack2_ent.AddComponent<Model>(backpack);
}

#ifdef FENRIR_PHYSICS
// The physics demo: a static ground, a stack of dynamic boxes and a kinematic paddle that game code sweeps back and
// forth in Tick, pushing the boxes. Each body is the unit cube in its own flat colour, so they are easy to tell apart.
// Beside them, a static backpack whose collider is a triangle mesh made from the backpack model's vertices, with boxes
// dropped onto it. F2 cycles the physics debug lines drawn over the active scene, whichever it is.
class PhysicsDemo
{
  public:
    PhysicsDemo(Fenrir::Physics& physics, GLRenderer& renderer) : m_physics(&physics), m_renderer(&renderer)
    {
    }

    void Init(Fenrir::App& app)
    {
        Fenrir::EntityList& entityList = app.GetActiveScene().GetEntityList();
        const Fenrir::Math::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);

        Fenrir::Entity physicsRoot = entityList.CreateEntity();
        physicsRoot.GetComponent<Fenrir::Name>().Set("Physics");

        // A collider with no RigidBody: static. Grey, its top face at y = -3.75.
        Fenrir::Entity ground = entityList.CreateEntity();
        ground.GetComponent<Fenrir::Name>().Set("Ground");
        ground.GetComponent<Fenrir::Transform>() = Fenrir::Transform(Fenrir::Math::Vec3(0.0f, -4.0f, -12.0f), identity,
                                                                     Fenrir::Math::Vec3(16.0f, 0.5f, 16.0f));
        ground.AddComponent<Fenrir::Collider>();
        ground.AddComponent<Material>(FlatColour(Fenrir::Math::Vec3(0.45f, 0.45f, 0.45f)));
        ground.AddComponent<Model>(cube);
        physicsRoot.AddChild(ground);

        for (std::size_t i = 0; i < boxColours.size(); ++i)
        {
            const float step = static_cast<float>(i);
            Fenrir::Entity box = entityList.CreateEntity();
            box.GetComponent<Fenrir::Name>().Set(("Box " + std::to_string(i)).c_str());
            box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(
                Fenrir::Math::Vec3(0.3f * static_cast<float>(i % 3) - 0.3f, 1.5f * step, -12.0f),
                Fenrir::Math::AngleAxis(0.3f * step, Fenrir::Math::Normalized(Fenrir::Math::Vec3(1.0f, 1.0f, 0.0f))),
                Fenrir::Math::Vec3(1.0f));
            box.AddComponent<Fenrir::RigidBody>();
            box.AddComponent<Fenrir::Collider>();
            box.AddComponent<Material>(FlatColour(boxColours[i]));
            box.AddComponent<Model>(cube);
            physicsRoot.AddChild(box);
        }

        // Kinematic: moved by MovePaddle in Tick, pushes the boxes and is not pushed. Orange.
        m_paddle = entityList.CreateEntity();
        m_paddle.GetComponent<Fenrir::Name>().Set("Paddle");
        m_paddle.GetComponent<Fenrir::Transform>() =
            Fenrir::Transform(Fenrir::Math::Vec3(-6.0f, -3.0f, -12.0f), identity, Fenrir::Math::Vec3(1.0f, 1.0f, 4.0f));
        m_paddle.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.motionType = Fenrir::MotionType::Kinematic});
        m_paddle.AddComponent<Fenrir::Collider>();
        m_paddle.AddComponent<Material>(FlatColour(Fenrir::Math::Vec3(1.0f, 0.55f, 0.0f)));
        m_paddle.AddComponent<Model>(cube);
        physicsRoot.AddChild(m_paddle);

        AddBackpack(entityList, physicsRoot);
    }

    // Game code in Tick: sweeps the paddle from x = -6 to 6 and back, once every 12.6 seconds.
    void MovePaddle(Fenrir::App& app)
    {
        m_time += app.GetTime().tickRate;
        m_paddle.GetComponent<Fenrir::Transform>().pos.x = static_cast<float>(-6.0 * std::cos(m_time * 0.5));
    }

    // In Update, between the renderer's drawing and the editor's: F2 moves on to the next category of debug lines, and
    // the lines the last step made are drawn in one pass. They show each body at its true step pose, so between ticks
    // a moving body's model, drawn at its interpolated pose, may trail its lines by up to a step.
    void DebugLines(Fenrir::App& app)
    {
        for (const KeyboardKeyEvent& event : m_keys.Read(app))
        {
            if (event.key == GLFW_KEY_F2 && event.state == InputState::Pressed)
            {
                m_debugMode = (m_debugMode + 1) % debugModes.size();
                app.Logger()->Info(std::string("Physics debug lines: ") + debugModes[m_debugMode].name);
            }
        }

        Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        if (world == nullptr)
        {
            return;
        }
        // The mode is the app's, but each scene's world keeps its own categories: a world that has just become active
        // may still have the mode it had when it was last shown, or none.
        if (world->DebugDrawCategories() != debugModes[m_debugMode].categories)
        {
            world->SetDebugDraw(debugModes[m_debugMode].categories);
        }

        m_lineVertices.clear();
        for (const Fenrir::DebugLine& line : world->DebugLines())
        {
            const Fenrir::Math::Vec3 colour = ColourOf(line.colour);
            m_lineVertices.push_back(LineVertex{line.from, colour});
            m_lineVertices.push_back(LineVertex{line.to, colour});
        }
        m_renderer->DrawLines(m_lineVertices);
    }

  private:
    struct DebugMode
    {
        Fenrir::DebugDraw categories;
        const char* name;
    };

    // What F2 steps through, starting from none.
    static constexpr std::array<DebugMode, 8> debugModes = {
        DebugMode{Fenrir::DebugDraw::None, "off"},
        DebugMode{Fenrir::DebugDraw::Colliders, "colliders"},
        DebugMode{Fenrir::DebugDraw::Contacts, "contacts"},
        DebugMode{Fenrir::DebugDraw::Velocities, "velocities"},
        DebugMode{Fenrir::DebugDraw::Bounds, "bounds"},
        DebugMode{Fenrir::DebugDraw::Joints, "joints"},
        DebugMode{Fenrir::DebugDraw::Characters, "characters"},
        DebugMode{Fenrir::DebugDraw::All, "all"},
    };

    // A debug line's 0xRRGGBBAA colour as the line shader takes it.
    static Fenrir::Math::Vec3 ColourOf(std::uint32_t colour)
    {
        const auto channel = [colour](unsigned shift) {
            return static_cast<float>((colour >> shift) & 0xFFu) / 255.0f;
        };
        return Fenrir::Math::Vec3(channel(24u), channel(16u), channel(8u));
    }

    // A static backpack, its collider a triangle mesh cooked from the model's vertex data: FenrirPhysics takes plain
    // vertices and indices, so it never sees the renderer's Model. Brown, standing on the ground in front of the stack,
    // with three boxes dropped onto it that come to rest on its shape, not on its bounding box.
    void AddBackpack(Fenrir::EntityList& entityList, Fenrir::Entity& physicsRoot)
    {
        std::vector<Fenrir::Math::Vec3> vertices;
        std::vector<std::uint32_t> indices;
        for (const Mesh& mesh : backpack.meshes)
        {
            const auto first = static_cast<std::uint32_t>(vertices.size());
            for (const Vertex& vertex : mesh.vertices)
            {
                vertices.push_back(vertex.pos);
            }
            for (const unsigned int index : mesh.indices)
            {
                indices.push_back(first + index);
            }
        }
        const Fenrir::CollisionMesh collisionMesh = m_physics->CreateCollisionMesh(vertices, indices);

        const Fenrir::Math::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        const Fenrir::Math::Vec3 base(5.0f, -3.75f - backpack.boundingBox.min.y, -7.0f);
        Fenrir::Entity bag = entityList.CreateEntity();
        bag.GetComponent<Fenrir::Name>().Set("Mesh collider (backpack)");
        bag.GetComponent<Fenrir::Transform>() = Fenrir::Transform(base, identity, Fenrir::Math::Vec3(1.0f));
        bag.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::MeshShape{collisionMesh}});
        bag.AddComponent<Material>(FlatColour(Fenrir::Math::Vec3(0.55f, 0.35f, 0.2f)));
        bag.AddComponent<Model>(backpack);
        physicsRoot.AddChild(bag);

        const float top = base.y + backpack.boundingBox.max.y;
        for (int i = 0; i < 3; ++i)
        {
            const float step = static_cast<float>(i);
            Fenrir::Entity box = entityList.CreateEntity();
            box.GetComponent<Fenrir::Name>().Set(("Backpack box " + std::to_string(i)).c_str());
            box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(
                base + Fenrir::Math::Vec3(0.6f * step - 0.6f, top - base.y + 2.0f + 1.5f * step, 0.0f), identity,
                Fenrir::Math::Vec3(0.5f));
            box.AddComponent<Fenrir::RigidBody>();
            box.AddComponent<Fenrir::Collider>();
            box.AddComponent<Material>(FlatColour(boxColours[static_cast<std::size_t>(i)]));
            box.AddComponent<Model>(cube);
            physicsRoot.AddChild(box);
        }
    }

    // The editor's debug shader draws one flat colour.
    static Material FlatColour(const Fenrir::Math::Vec3& colour)
    {
        Material material{aabbShader, {}};
        material.properties["color"] = colour;
        return material;
    }

    // Not constexpr: MSVC does not take glm's vec3 constructor as one.
    inline static const std::array<Fenrir::Math::Vec3, 8> boxColours = {
        Fenrir::Math::Vec3(0.90f, 0.20f, 0.20f), // red
        Fenrir::Math::Vec3(0.20f, 0.75f, 0.25f), // green
        Fenrir::Math::Vec3(0.20f, 0.40f, 0.95f), // blue
        Fenrir::Math::Vec3(0.95f, 0.90f, 0.20f), // yellow
        Fenrir::Math::Vec3(0.70f, 0.25f, 0.85f), // purple
        Fenrir::Math::Vec3(0.20f, 0.85f, 0.85f), // cyan
        Fenrir::Math::Vec3(0.95f, 0.45f, 0.70f), // pink
        Fenrir::Math::Vec3(0.95f, 0.95f, 0.95f), // white
    };

    Fenrir::Physics* m_physics;
    GLRenderer* m_renderer;
    Fenrir::Entity m_paddle;
    double m_time = 0.0;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
    std::size_t m_debugMode = 0;
    std::vector<LineVertex> m_lineVertices;
};

#define BIND_PHYSICS_DEMO_FN(fn, demoInstance) std::bind(&PhysicsDemo::fn, &demoInstance, std::placeholders::_1)
#endif

class AssetLoader
{
  public:
    AssetLoader(Fenrir::ILogger& logger, std::string& assetPath, GLRenderer& renderer)
        : m_assetPath(assetPath), m_shaderLibrary(logger), m_textureLibrary(logger),
          m_modelLibrary(logger, m_textureLibrary, m_shaderLibrary), m_renderer(renderer)
    {
    }

    void Init(Fenrir::App&)
    {
        m_shaderLibrary.AddShader("lightedObject", m_assetPath + "shaders/lighted_vertex.glsl",
                                  m_assetPath + "shaders/lighted_fragment.glsl");
        m_shaderLibrary.AddShader("light", m_assetPath + "shaders/light_vertex.glsl",
                                  m_assetPath + "shaders/light_fragment.glsl");
        m_shaderLibrary.AddShader("debug", m_assetPath + "shaders/debug_vertex.glsl",
                                  m_assetPath + "shaders/debug_fragment.glsl");
        m_shaderLibrary.AddShader("lines", m_assetPath + "shaders/lines_vertex.glsl",
                                  m_assetPath + "shaders/lines_fragment.glsl");
        m_shaderLibrary.AddShader("skybox", m_assetPath + "shaders/skybox_vertex.glsl",
                                  m_assetPath + "shaders/skybox_fragment.glsl");
        m_shaderLibrary.AddShader("shaded", m_assetPath + "shaders/shaded_vertex.glsl",
                                  m_assetPath + "shaders/shaded_fragment.glsl");

        myShader = m_shaderLibrary.GetShader("lightedObject");
        lightShader = m_shaderLibrary.GetShader("light");
        aabbShader = m_shaderLibrary.GetShader("debug");
        skyboxShader = m_shaderLibrary.GetShader("skybox");
        shadedShader = m_shaderLibrary.GetShader("shaded");

        m_modelLibrary.AddModel(m_assetPath + "models/backpack/backpack.obj");

        backpack = m_modelLibrary.GetModel(m_assetPath + "models/backpack/backpack.obj");

        m_modelLibrary.AddModel(m_assetPath + "models/cube/cube.obj");
        cube = m_modelLibrary.GetModel(m_assetPath + "models/cube/cube.obj");

        m_modelLibrary.AddModel(m_assetPath + "models/sphere/sphere.obj");
        sphere = m_modelLibrary.GetModel(m_assetPath + "models/sphere/sphere.obj");

        std::string skyboxPath = m_assetPath + "textures/skybox/";
        m_textureLibrary.AddSkybox(skyboxPath, skyboxPath + "top.png", skyboxPath + "bottom.png",
                                   skyboxPath + "left.png", skyboxPath + "right.png", skyboxPath + "front.png",
                                   skyboxPath + "back.png");

        skybox = m_textureLibrary.GetSkybox(skyboxPath);

        m_textureLibrary.AddTexture(m_assetPath + "textures/icons/file.png");
        m_textureLibrary.AddTexture(m_assetPath + "textures/icons/folder.png");
        m_textureLibrary.AddTexture(m_assetPath + "textures/icons/fenrir.png");

        m_renderer.SetSkybox(skybox);
        m_renderer.SetSkyboxShader(skyboxShader);
        Shader lineShader = m_shaderLibrary.GetShader("lines");
        m_renderer.SetLineShader(lineShader);
    }

    TextureLibrary& GetTextureLibrary()
    {
        return m_textureLibrary;
    }

  private:
    std::string m_assetPath;
    ShaderLibrary m_shaderLibrary;
    TextureLibrary m_textureLibrary;
    ModelLibrary m_modelLibrary;
    GLRenderer& m_renderer;
};

#define BIND_ASSET_LOADER_FN(fn, assetLoaderInstance) \
    std::bind(&AssetLoader::fn, &assetLoaderInstance, std::placeholders::_1)

int main(int argc, char* argv[])
{

    auto logger = std::make_unique<Fenrir::ConsoleLogger>();

    ProjectSettings projectSettings{};
    std::string fileContents;
    auto ec = glz::read_file_json(projectSettings, "assets/demoApp.feproj", fileContents); // TODO error handling
    if (ec)
    {
        logger->Fatal("Failed to load project settings: {0}", glz::format_error(ec, fileContents));
        return -1;
    }

    Fenrir::App app(std::move(logger));

    // One F-key per feature family steps through its scenes and back to Default (DemoScenes.hpp). Each scene below
    // registers here as it is built in. Before anything else of the frame, so the frame runs in the scene switched to.
#ifdef FENRIR_PHYSICS
    DemoScenes demoScenes(
        "Default", "WASD moves the camera, F1 turns its controls off and on, F2 cycles the physics debug lines.");
#else
    DemoScenes demoScenes("Default", "WASD moves the camera, F1 turns its controls off and on.");
#endif
    app.AddSequentialSystems(Fenrir::SchedulePriority::PreUpdate,
                             {[&demoScenes](Fenrir::App& running) { demoScenes.Update(running); }});

#ifdef FENRIR_PHYSICS
    // Declared after the app, so every scene's physics world and the physics backend end before the app does. Before
    // scripting, which is given physics's scripting extension, so scripting ends first.
    auto physics = Fenrir::Physics::Install(app);
#endif

#ifdef FENRIR_SCRIPTING
    // Declared after the app, so .NET is released before the app is destroyed and before main returns.
    // The components and events C++ and C# share, as the app's own scripting extension, and physics's, so scripts can
    // use physics.
    Fenrir::ScriptingConfig scriptingConfig{.scriptAssembly = projectSettings.scriptAssembly,
                                            .extensions = {ScriptingDemo::Extension()}};
#ifdef FENRIR_PHYSICS
    scriptingConfig.extensions.push_back(physics.ScriptingExtension());
#endif
    auto scripting = Fenrir::Scripting::Install(app, std::move(scriptingConfig));
    // C# systems run in the Update phase when Install put them there; these run after them, so they see what C# just
    // wrote. DemoMeshes gives what C# made visible its Model and Material before the renderer draws.
    DemoMeshes demoMeshes(cube, sphere, shadedShader);
    app.AddSequentialSystems(Fenrir::SchedulePriority::Update,
                             {ScriptingDemo::ShowBobPhase, ScriptingDemo::LogBursts(), ScriptingDemo::AutoPlay(),
                              [&demoMeshes](Fenrir::App& running) { demoMeshes.Update(running); }});
#ifdef FENRIR_PHYSICS
    // The second scene, built and run by C# (scripts/Physics/Playground.cs): C++ makes it empty but for a marker, and
    // F3 switches to it and back.
    app.AddSystem(Fenrir::SchedulePriority::PostInit, [](Fenrir::App& running) {
        CSharpScenes::Create(running, CSharpScenes::physicsName, CSharpScenes::physicsId);
    });
    demoScenes.Register(CSharpScenes::Physics());
#endif
#endif

    Fenrir::Camera camera;
    Window window(projectSettings.name + " v" + projectSettings.version);

    CameraController cameraController(camera, 0.1f, 3.0f);

    GLRenderer glRenderer(*app.Logger().get(), window, camera);

#ifdef FENRIR_PHYSICS
    PhysicsDemo physicsDemo(physics, glRenderer);
    // PostInit: the cube and the debug shader are loaded in Init.
    app.AddSystem(Fenrir::SchedulePriority::PostInit, BIND_PHYSICS_DEMO_FN(Init, physicsDemo))
        .AddSequentialSystems(Fenrir::SchedulePriority::Tick, {BIND_PHYSICS_DEMO_FN(MovePaddle, physicsDemo)});

    // Joints (C++), the first of the F4 family. Made in PostInit, like the physics demo, once the cube is loaded.
    JointsScene jointsScene(cube, aabbShader);
    app.AddSystem(Fenrir::SchedulePriority::PostInit,
                  [&jointsScene](Fenrir::App& running) { jointsScene.Init(running); })
        .AddSequentialSystems(Fenrir::SchedulePriority::Update,
                              {[&jointsScene](Fenrir::App& running) { jointsScene.Update(running); }});
    demoScenes.Register(JointsScene::Scene());
#ifdef FENRIR_SCRIPTING
    // Joints (C#), the second of the F4 family, built and run by C# (scripts/Physics/Joints.cs) as Physics (C#) is.
    app.AddSystem(Fenrir::SchedulePriority::PostInit, [](Fenrir::App& running) {
        CSharpScenes::Create(running, CSharpScenes::jointsName, CSharpScenes::jointsId);
    });
    demoScenes.Register(CSharpScenes::Joints());
#endif

    // Character (C++), the first of the F5 family, made in PostInit once the cube is loaded. Its camera writes the
    // scene's DemoCamera in Update, before DemoCameras copies it.
    CharacterScene characterScene(physics, cube, aabbShader);
    app.AddSystem(Fenrir::SchedulePriority::PostInit,
                  [&characterScene](Fenrir::App& running) { characterScene.Init(running); })
        .AddSequentialSystems(Fenrir::SchedulePriority::Tick,
                              {[&characterScene](Fenrir::App& running) { characterScene.Tick(running); }})
        .AddSequentialSystems(Fenrir::SchedulePriority::Update,
                              {[&characterScene](Fenrir::App& running) { characterScene.Update(running); }});
    demoScenes.Register(CharacterScene::Scene());
#ifdef FENRIR_SCRIPTING
    // Character (C#), the second of the F5 family, built and run by C# (scripts/Physics/Character.cs), with its own C#
    // follow camera writing the DemoCamera it makes.
    app.AddSystem(Fenrir::SchedulePriority::PostInit, [](Fenrir::App& running) {
        CSharpScenes::Create(running, CSharpScenes::characterName, CSharpScenes::characterId);
    });
    demoScenes.Register(CSharpScenes::Character());
#endif

    // Vehicles (C++), the first of the F6 family, made in PostInit once the models are loaded. Its chase camera
    // writes the scene's DemoCamera in Update, before DemoCameras copies it.
    VehicleScene vehicleScene(cube, sphere, aabbShader);
    app.AddSystem(Fenrir::SchedulePriority::PostInit,
                  [&vehicleScene](Fenrir::App& running) { vehicleScene.Init(running); })
        .AddSequentialSystems(Fenrir::SchedulePriority::Tick,
                              {[&vehicleScene](Fenrir::App& running) { vehicleScene.Tick(running); }})
        .AddSequentialSystems(Fenrir::SchedulePriority::Update,
                              {[&vehicleScene](Fenrir::App& running) { vehicleScene.Update(running); }});
    demoScenes.Register(VehicleScene::Scene());
#endif

    // After the free-fly camera controller and the C# Update systems: a scene's DemoCamera, while active, wins.
    DemoCameras demoCameras(camera);

    AssetLoader assetLoader(*app.Logger().get(), projectSettings.assetPath, glRenderer);

    Editor editor(app, *app.Logger().get(), window, glRenderer, camera, assetLoader.GetTextureLibrary());
    editor.SetProjectSettings(projectSettings);
    app.Logger()->AddLogger(editor.GetLogger());

    app.AddSystems(Fenrir::SchedulePriority::PreInit, {BIND_WINDOW_SYSTEM_FN(Window::PreInit, window)})
        .AddSystems(Fenrir::SchedulePriority::Init, {BIND_GL_RENDERER_FN(GLRenderer::Init, glRenderer),
                                                     BIND_ASSET_LOADER_FN(AssetLoader::Init, assetLoader),
                                                     BIND_EDITOR_FN(Editor::Init, editor), InitLights, InitBackpacks})
        // .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        .AddSequentialSystems(
            Fenrir::SchedulePriority::PreUpdate,
            {BIND_EDITOR_FN(Editor::PreUpdate, editor), BIND_GL_RENDERER_FN(GLRenderer::PreUpdate, glRenderer)})

        .AddSystems(Fenrir::SchedulePriority::Update,
                    {BIND_CAMERA_CONTROLLER_FN(CameraController::Update, cameraController)})
        .AddSequentialSystems(Fenrir::SchedulePriority::Update,
                              {[&demoCameras](Fenrir::App& running) { demoCameras.Update(running); }})
        // The physics debug lines are drawn after the scene and before the editor unbinds the scene's framebuffer.
        .AddSequentialSystems(Fenrir::SchedulePriority::Update, {BIND_GL_RENDERER_FN(GLRenderer::Update, glRenderer),
#ifdef FENRIR_PHYSICS
                                                                 BIND_PHYSICS_DEMO_FN(DebugLines, physicsDemo),
#endif
                                                                 BIND_EDITOR_FN(Editor::Update, editor)})
        // .AddSystems(Fenrir::SchedulePriority::Tick, {Tick})
        .AddSequentialSystems(Fenrir::SchedulePriority::PostUpdate,
                              {BIND_GL_RENDERER_FN(GLRenderer::PostUpdate, glRenderer),
                               BIND_WINDOW_SYSTEM_FN(Window::PostUpdate, window)})
        .AddSequentialSystems(Fenrir::SchedulePriority::Exit,
                              {BIND_EDITOR_FN(Editor::Exit, editor), BIND_WINDOW_SYSTEM_FN(Window::Exit, window),
                               BIND_GL_RENDERER_FN(GLRenderer::Exit, glRenderer)})
        .Run();
}