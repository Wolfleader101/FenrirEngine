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

#include <glad/glad.h>

#include <iostream>
#include <variant>

#include <entt/container/dense_map.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// #include <glaze/glaze.hpp>

struct ProjectSettings
{
    std::string identity = "";
    std::string name = "";
    std::string version = "0.1.0";
    std::string description = "";
    std::string author = "";
    std::string assetPath = "";
};

// template <>
// struct glz::meta<ProjectSettings>
// {
//     using T = ProjectSettings;
//     static constexpr auto value =
//         object(&T::identity, &T::name, &T::version, &T::description, &T::author, &T::assetPath);
// };

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

using MaterialProperty = std::variant<bool, int, float, Fenrir::Math::Vec2, Fenrir::Math::Vec3, Fenrir::Math::Vec4,
                                      Fenrir::Math::Mat3, Fenrir::Math::Mat4>;

struct Material
{
    Shader shader;
    entt::dense_map<std::string, MaterialProperty> properties;
};

Shader myShader;
Shader lightShader;

Model backpack;

Model cube;

const glm::vec3 pointLightPositions[4] = {glm::vec3(0.7f, 0.2f, 2.0f), glm::vec3(2.3f, -3.3f, -4.0f),
                                          glm::vec3(-4.0f, 2.0f, -12.0f), glm::vec3(0.0f, 0.0f, -3.0f)};

void InitLights(Fenrir::App& app)
{

    Fenrir::EntityList& entityList = app.GetActiveScene().GetEntityList();

    Fenrir::Entity light1_ent = entityList.CreateEntity();
    Fenrir::Entity light2_ent = entityList.CreateEntity();
    Fenrir::Entity light3_ent = entityList.CreateEntity();
    Fenrir::Entity light4_ent = entityList.CreateEntity();

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

void InitBackpacks(Fenrir::App& app)
{
    Fenrir::EntityList& entityList = app.GetActiveScene().GetEntityList();

    Fenrir::Entity backpack1_ent = entityList.CreateEntity();
    Fenrir::Entity backpack2_ent = entityList.CreateEntity();

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

class GLRenderer
{
  public:
    GLRenderer(Fenrir::ILogger& logger, Window& window, Fenrir::Camera& camera)
        : m_logger(logger), m_window(window), m_camera(camera)
    {
    }

    void Init(Fenrir::App& app)
    {
        m_logger.Info("GLRenderer::Init - Initializing GLRenderer");

        //? GL SPECIFIC CODE FOR TESTING
        glViewport(0, 0, m_window.GetWidth(), m_window.GetHeight());

        glEnable(GL_DEPTH_TEST);
    }

    void PreUpdate(Fenrir::App& app)
    {
        glClearColor(0.45f, 0.6f, 0.75f, 1.0f); // vakol blue
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe mode
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fill mode
    }

    void Update(Fenrir::App& app)
    {
        m_view = m_camera.GetViewMatrix();

        // TODO might only want to recalculate this if it changes? (could use events)
        m_projection =
            Fenrir::Math::Perspective(Fenrir::Math::DegToRad(m_camera.fov),
                                      static_cast<float>(m_window.GetWidth() / m_window.GetHeight()), 0.1f, 100.0f);

        // TODO above needs to be moved somehow

        Fenrir::EntityList& entityList = app.GetActiveScene().GetEntityList();

        entityList.ForEach<Fenrir::Transform, Model, Material>(
            [&](Fenrir::Transform& transform, Model& model, Material& material) {
                material.properties["spotLight.pos"] = m_camera.pos;
                material.properties["spotLight.direction"] = m_camera.front;
                SetMatProps(material.shader, material);
                DrawModel(transform, model, material.shader);
            });
    }

    void PostUpdate(Fenrir::App& app)
    {
    }

    void Exit(Fenrir::App& app)
    {
    }

  private:
    Fenrir::ILogger& m_logger;
    Window& m_window;
    Fenrir::Camera& m_camera;

    Fenrir::Math::Mat4 m_view;
    Fenrir::Math::Mat4 m_projection;

    void SetMatProps(const Shader& shader, const Material& mat)
    {
        mat.shader.Use();
        for (const auto [name, property] : mat.properties)
        {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;

                    if constexpr (std::is_same_v<T, bool>)
                    {
                        shader.SetBool(name, value);
                    }
                    else if constexpr (std::is_same_v<T, int>)
                    {
                        shader.SetInt(name, value);
                    }
                    else if constexpr (std::is_same_v<T, float>)
                    {
                        shader.SetFloat(name, value);
                    }
                    else if constexpr (std::is_same_v<T, Fenrir::Math::Vec2>)
                    {
                        shader.SetVec2(name, value);
                    }
                    else if constexpr (std::is_same_v<T, Fenrir::Math::Vec3>)
                    {
                        shader.SetVec3(name, value);
                    }
                    else if constexpr (std::is_same_v<T, Fenrir::Math::Vec4>)
                    {
                        shader.SetVec4(name, value);
                    }
                    else if constexpr (std::is_same_v<T, Fenrir::Math::Mat3>)
                    {
                        shader.SetMat3(name, value);
                    }
                    else if constexpr (std::is_same_v<T, Fenrir::Math::Mat4>)
                    {
                        shader.SetMat4(name, value);
                    }
                    else
                    {
                        m_logger.Fatal("GLRenderer::SetMatProps - Unknown material property type for {0}", name);
                    }
                },
                property);
        }
    }

    void DrawModel(Fenrir::Transform& transform, Model& model, Shader& shader)
    {
        Fenrir::Math::Mat4 mdl_mat = Fenrir::Math::Mat4(1.0f);

        mdl_mat = Fenrir::Math::Translate(mdl_mat, transform.pos);

        mdl_mat *= Fenrir::Math::Mat4Cast(transform.rot);

        mdl_mat = Fenrir::Math::Scale(mdl_mat, transform.scale);

        shader.Use();
        shader.SetMat4("view", m_view);
        shader.SetMat4("projection", m_projection);
        shader.SetMat4("model", mdl_mat);

        for (auto& mesh : model.meshes)
        {
            DrawMesh(mesh, shader);
        }
    }

    void DrawMesh(Mesh& mesh, Shader& shader)
    {
        unsigned int diffuseNr = 1;
        unsigned int specularNr = 1;

        for (unsigned int i = 0; i < mesh.textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);

            // retrieve texture number (the N in diffuse_textureN)
            std::string number;
            std::string name = "material.";
            TextureType type = mesh.textures[i].type;
            if (type == TextureType::Diffuse)
            {
                number = std::to_string(diffuseNr++);
                name += "diffuse";
            }
            else if (type == TextureType::Specular)
            {
                number = std::to_string(specularNr++);
                name += "specular";
            }

            shader.SetInt((name + number).c_str(), i);
            glBindTexture(GL_TEXTURE_2D, mesh.textures[i].Id);
        }

        // draw mesh
        glBindVertexArray(mesh.VAO);
        glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE0);
    }
};
#define BIND_GL_RENDERER_FN(fn, glRendererInstance) \
    std::bind(&GLRenderer::fn, &glRendererInstance, std::placeholders::_1)

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

        myShader = m_shaderLibrary.GetShader("lightedObject");
        lightShader = m_shaderLibrary.GetShader("light");

        m_modelLibrary.AddModel(m_assetPath + "models/backpack/backpack.obj");

        backpack = m_modelLibrary.GetModel(m_assetPath + "models/backpack/backpack.obj");

        m_modelLibrary.AddModel(m_assetPath + "models/cube/cube.obj");
        cube = m_modelLibrary.GetModel(m_assetPath + "models/cube/cube.obj");
    }

  private:
    std::string m_assetPath;
    ShaderLibrary m_shaderLibrary;
    TextureLibrary m_textureLibrary;
    ModelLibrary m_modelLibrary;
};

#define BIND_ASSET_LOADER_FN(fn, assetLoaderInstance) \
    std::bind(&AssetLoader::fn, &assetLoaderInstance, std::placeholders::_1)

void Tick(Fenrir::App& app)
{
    //     Fenrir::EntityList& entityList = app.GetActiveScene().GetEntityList();

    //     entityList.ForEach<Fenrir::Transform, Model, Material>(
    //         [&](Fenrir::Transform& transform, Model& model, Material& material) {
    //             Fenrir::Math::Vec3 newPos =
    //                 transform.pos + Fenrir::Math::Vec3(0.0f, 1.f, 0.0f) * static_cast<float>(app.GetTime().tickRate);
    //             transform.pos = newPos;
    //         });
}

class Editor
{
  public:
    Editor(Fenrir::ILogger& logger, Window& window, Fenrir::Camera& camera)
        : m_logger(logger), m_window(window), m_camera(camera)
    {
    }

    void Init(Fenrir::App& app)
    {
        m_logger.Info("Editor::Init - Initializing Editor");

        // setup imgui
        IMGUI_CHECKVERSION();
        m_guiContext = ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-viewport

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForOpenGL(m_window.GetGlWindow(), true); // Takes in the GLFW Window
        ImGui_ImplOpenGL3_Init("#version 460");                     // Sets the version of GLSL being used
    }

    void PreUpdate(Fenrir::App& app)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void Update(Fenrir::App& app)
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text."); // Display some text (you can use a format strings too)

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f); // Edit 1 float using a slider from 0.0f to 1.0f

        if (ImGui::Button(
                "Button")) // Buttons return true when clicked (most widgets return true when edited/activated)
            counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::End();
    }

    void PostUpdate(Fenrir::App& app)
    {
        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // handle the viewports
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
    }

    void Exit(Fenrir::App& app)
    {
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::DestroyPlatformWindows();
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_guiContext);
    }

  private:
    Fenrir::ILogger& m_logger;
    Window& m_window;
    Fenrir::Camera& m_camera;
    ImGuiContext* m_guiContext;
};
#define BIND_EDITOR_FN(fn, editorInstance) std::bind(&Editor::fn, &editorInstance, std::placeholders::_1)

int main()
{
    ProjectSettings projectSettings{};
    // auto ec = glz::read_file_json(projectSettings, "assets/demoApp.feproj", std::string{}); // TODO error handling
    //! HARDCODED WHILE GLZ DOENST WORK
    projectSettings.identity = "fenrir.examples.demo.app";
    projectSettings.name = "Demo App";
    projectSettings.version = "0.1.0";
    projectSettings.description = "";
    projectSettings.author = "";
    projectSettings.assetPath = "assets/";

    auto logger = std::make_unique<Fenrir::ConsoleLogger>();
    Fenrir::App app(std::move(logger));

    Fenrir::Camera camera;
    Window window(projectSettings.name + " v" + projectSettings.version);

    CameraController cameraController(camera, 0.1f, 3.0f);

    GLRenderer glRenderer(*app.Logger().get(), window, camera);

    AssetLoader assetLoader(*app.Logger().get(), projectSettings.assetPath);

    Editor editor(*app.Logger().get(), window, camera);

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
        .AddSystems(Fenrir::SchedulePriority::Tick, {Tick})
        .AddSequentialSystems(Fenrir::SchedulePriority::PostUpdate,
                              {BIND_GL_RENDERER_FN(GLRenderer::PostUpdate, glRenderer),
                               BIND_EDITOR_FN(Editor::PostUpdate, editor),
                               BIND_WINDOW_SYSTEM_FN(Window::PostUpdate, window)})
        .AddSequentialSystems(Fenrir::SchedulePriority::Exit,
                              {BIND_EDITOR_FN(Editor::Exit, editor), BIND_WINDOW_SYSTEM_FN(Window::Exit, window),
                               BIND_GL_RENDERER_FN(GLRenderer::Exit, glRenderer)})
        .Run();
}