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

#include "Editor.hpp"
#include "GLRenderer.hpp"
#include "Material.hpp"

#include <glaze/glaze.hpp>

struct ProjectSettings
{
    std::string identity = "";
    std::string name = "";
    std::string version = "0.1.0";
    std::string description = "";
    std::string author = "";
    std::string assetPath = "";
};

template <>
struct glz::meta<ProjectSettings>
{
    using T = ProjectSettings;
    static constexpr auto value =
        object(&T::identity, &T::name, &T::version, &T::description, &T::author, &T::assetPath);
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

static Skybox skybox;

static Model backpack;
static Model cube;

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

class AssetLoader
{
  public:
    AssetLoader(Fenrir::ILogger& logger, std::string& assetPath)
        : m_assetPath(assetPath), m_shaderLibrary(logger), m_textureLibrary(logger),
          m_modelLibrary(logger, m_textureLibrary, m_shaderLibrary)
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
        m_shaderLibrary.AddShader("skybox", m_assetPath + "shaders/skybox_vertex.glsl",
                                  m_assetPath + "shaders/skybox_fragment.glsl");

        myShader = m_shaderLibrary.GetShader("lightedObject");
        lightShader = m_shaderLibrary.GetShader("light");
        aabbShader = m_shaderLibrary.GetShader("debug");
        skyboxShader = m_shaderLibrary.GetShader("skybox");

        m_modelLibrary.AddModel(m_assetPath + "models/backpack/backpack.obj");

        backpack = m_modelLibrary.GetModel(m_assetPath + "models/backpack/backpack.obj");

        m_modelLibrary.AddModel(m_assetPath + "models/cube/cube.obj");
        cube = m_modelLibrary.GetModel(m_assetPath + "models/cube/cube.obj");

        std::string skyboxPath = m_assetPath + "textures/skybox/";
        m_textureLibrary.AddSkybox(skyboxPath, skyboxPath + "top.png", skyboxPath + "bottom.png",
                                   skyboxPath + "left.png", skyboxPath + "right.png", skyboxPath + "front.png",
                                   skyboxPath + "back.png");

        skybox = m_textureLibrary.GetSkybox(skyboxPath);
    }

  private:
    std::string m_assetPath;
    ShaderLibrary m_shaderLibrary;
    TextureLibrary m_textureLibrary;
    ModelLibrary m_modelLibrary;
};

#define BIND_ASSET_LOADER_FN(fn, assetLoaderInstance) \
    std::bind(&AssetLoader::fn, &assetLoaderInstance, std::placeholders::_1)

int main()
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

    Fenrir::Camera camera;
    Window window(projectSettings.name + " v" + projectSettings.version);

    CameraController cameraController(camera, 0.1f, 3.0f);

    GLRenderer glRenderer(*app.Logger().get(), window, camera);

    AssetLoader assetLoader(*app.Logger().get(), projectSettings.assetPath);

    glRenderer.SetSkybox(skybox);
    glRenderer.SetSkyboxShader(skyboxShader);

    Editor editor(app, *app.Logger().get(), window, glRenderer, camera);

    app.AddSystems(Fenrir::SchedulePriority::PreInit, {BIND_WINDOW_SYSTEM_FN(Window::PreInit, window)})
        .AddSystems(Fenrir::SchedulePriority::Init,
                    {BIND_GL_RENDERER_FN(GLRenderer::Init, glRenderer), BIND_EDITOR_FN(Editor::Init, editor),
                     BIND_ASSET_LOADER_FN(AssetLoader::Init, assetLoader), InitLights, InitBackpacks})
        // .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        .AddSequentialSystems(
            Fenrir::SchedulePriority::PreUpdate,
            {BIND_EDITOR_FN(Editor::PreUpdate, editor), BIND_GL_RENDERER_FN(GLRenderer::PreUpdate, glRenderer)})

        .AddSystems(Fenrir::SchedulePriority::Update,
                    {BIND_CAMERA_CONTROLLER_FN(CameraController::Update, cameraController)})
        .AddSequentialSystems(Fenrir::SchedulePriority::Update, {BIND_GL_RENDERER_FN(GLRenderer::Update, glRenderer),
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