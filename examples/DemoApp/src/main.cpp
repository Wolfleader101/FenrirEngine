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

#include <glad/glad.h>

struct Transform
{
    Fenrir::Math::Vec3 pos;
    Fenrir::Math::Quat rot;
    Fenrir::Math::Vec3 scale;
};

float cubeVertices[288] = {
    // positions          // normals           // texture coords
    -0.5f, -0.5f, -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f, 0.0f, 0.5f,  -0.5f, -0.5f, 0.0f,  0.0f,  -1.0f, 1.0f, 0.0f,
    0.5f,  0.5f,  -0.5f, 0.0f,  0.0f,  -1.0f, 1.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 0.0f,  0.0f,  -1.0f, 1.0f, 1.0f,
    -0.5f, 0.5f,  -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f, 0.0f,

    -0.5f, -0.5f, 0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
    0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f, 0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
    -0.5f, 0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f, -0.5f, -0.5f, 0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

    -0.5f, 0.5f,  0.5f,  -1.0f, 0.0f,  0.0f,  1.0f, 0.0f, -0.5f, 0.5f,  -0.5f, -1.0f, 0.0f,  0.0f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f, 0.0f,  0.0f,  0.0f, 1.0f, -0.5f, -0.5f, -0.5f, -1.0f, 0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, 0.5f,  -1.0f, 0.0f,  0.0f,  0.0f, 0.0f, -0.5f, 0.5f,  0.5f,  -1.0f, 0.0f,  0.0f,  1.0f, 0.0f,

    0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.5f,  0.5f,  -0.5f, 1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
    0.5f,  -0.5f, -0.5f, 1.0f,  0.0f,  0.0f,  0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    0.5f,  -0.5f, 0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f, 0.0f,  -1.0f, 0.0f,  0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 0.0f,  -1.0f, 0.0f,  1.0f, 1.0f,
    0.5f,  -0.5f, 0.5f,  0.0f,  -1.0f, 0.0f,  1.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  0.0f,  -1.0f, 0.0f,  1.0f, 0.0f,
    -0.5f, -0.5f, 0.5f,  0.0f,  -1.0f, 0.0f,  0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f,  -1.0f, 0.0f,  0.0f, 1.0f,

    -0.5f, 0.5f,  -0.5f, 0.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
    0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f, 0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f, 0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 0.0f,  1.0f,  0.0f,  0.0f, 1.0f};

Shader myShader;

const glm::vec3 pointLightPositions[4] = {glm::vec3(0.7f, 0.2f, 2.0f), glm::vec3(2.3f, -3.3f, -4.0f),
                                          glm::vec3(-4.0f, 2.0f, -12.0f), glm::vec3(0.0f, 0.0f, -3.0f)};

unsigned int cubeIndices[36] = {
    // Back face
    0, 1, 2, 0, 2, 3,
    // Front face
    4, 5, 6, 4, 6, 7,
    // Left face
    8, 9, 10, 8, 10, 11,
    // Right face
    12, 13, 14, 12, 14, 15,
    // Bottom face
    16, 17, 18, 16, 18, 19,
    // Top face
    20, 21, 22, 20, 22, 23};

unsigned int cubeVBO;
unsigned int cubeVAO;
unsigned int cubeEBO;

Texture cubeDiffuse;
Texture cubeSpecular;

Model backpack;
Model backpack2;

Transform backpackTransform = {Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f), Fenrir::Math::Quat(0.0f, 0.0f, 0.0f, 1.0f),
                               Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f)};

Transform backpack2Transform = {Fenrir::Math::Vec3(0.0f, 5.0f, 0.0f), Fenrir::Math::Quat(0.0f, 0.0f, 0.0f, 1.0f),
                                Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f)};

void InitCubes(Fenrir::App& app)
{
    // //! VERTEX DATA AND BUFFERS
    // create a vertex buffer object, store its ID in VBO
    glGenBuffers(1, &cubeVBO);

    // bind the buffer to the GL_ARRAY_BUFFER target (any calls made to GL_ARRAY_BUFFER will affect VBO)
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);

    // copy the vertex data into the buffer's memory
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    // create an element buffer object, store its ID in EBO
    glGenBuffers(1, &cubeEBO);

    // bind the buffer to the GL_ELEMENT_ARRAY_BUFFER target
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);

    // copy the vertex data into the buffer's memory
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

    // create a vertex array object, store its ID in VAO
    glGenVertexArrays(1, &cubeVAO);

    // initialise VAO only once
    glBindVertexArray(cubeVAO);

    // bind the vertex buffer object
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    // bind the element buffer object
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

    // tell OpenGL how to interpret the vertex data (per vertex attribute)

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    // normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // texture coord attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // unbind the VBO and VAO
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    // unbinding is not always needed as a VAO is is created and bound before other objects are bound to it
}

unsigned int lightVAO;
Shader lightShader;

void InitLights(Fenrir::App& app)
{

    glGenVertexArrays(1, &lightVAO);
    glBindVertexArray(lightVAO);

    // use pre exising VBO
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
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

        //! HARD CODE SHADER FOR NOW
        myShader.Use();

        myShader.SetMat4("view", m_view);
        myShader.SetMat4("projection", m_projection);

        // directional light
        myShader.SetVec3("dirLight.direction", -0.2f, -1.0f, -0.3f);
        myShader.SetVec3("dirLight.ambient", 0.05f, 0.05f, 0.05f);
        myShader.SetVec3("dirLight.diffuse", 0.4f, 0.4f, 0.4f);
        myShader.SetVec3("dirLight.specular", 0.5f, 0.5f, 0.5f);
        // point light 1
        myShader.SetVec3("pointLights[0].pos", pointLightPositions[0]);
        myShader.SetVec3("pointLights[0].ambient", 0.05f, 0.05f, 0.05f);
        myShader.SetVec3("pointLights[0].diffuse", 0.8f, 0.8f, 0.8f);
        myShader.SetVec3("pointLights[0].specular", 1.0f, 1.0f, 1.0f);
        myShader.SetFloat("pointLights[0].constant", 1.0f);
        myShader.SetFloat("pointLights[0].linear", 0.09f);
        myShader.SetFloat("pointLights[0].quadratic", 0.032f);
        // point light 2
        myShader.SetVec3("pointLights[1].pos", pointLightPositions[1]);
        myShader.SetVec3("pointLights[1].ambient", 0.05f, 0.05f, 0.05f);
        myShader.SetVec3("pointLights[1].diffuse", 0.8f, 0.8f, 0.8f);
        myShader.SetVec3("pointLights[1].specular", 1.0f, 1.0f, 1.0f);
        myShader.SetFloat("pointLights[1].constant", 1.0f);
        myShader.SetFloat("pointLights[1].linear", 0.09f);
        myShader.SetFloat("pointLights[1].quadratic", 0.032f);
        // point light 3
        myShader.SetVec3("pointLights[2].pos", pointLightPositions[2]);
        myShader.SetVec3("pointLights[2].ambient", 0.05f, 0.05f, 0.05f);
        myShader.SetVec3("pointLights[2].diffuse", 0.8f, 0.8f, 0.8f);
        myShader.SetVec3("pointLights[2].specular", 1.0f, 1.0f, 1.0f);
        myShader.SetFloat("pointLights[2].constant", 1.0f);
        myShader.SetFloat("pointLights[2].linear", 0.09f);
        myShader.SetFloat("pointLights[2].quadratic", 0.032f);
        // point light 4
        myShader.SetVec3("pointLights[3].pos", pointLightPositions[3]);
        myShader.SetVec3("pointLights[3].ambient", 0.05f, 0.05f, 0.05f);
        myShader.SetVec3("pointLights[3].diffuse", 0.8f, 0.8f, 0.8f);
        myShader.SetVec3("pointLights[3].specular", 1.0f, 1.0f, 1.0f);
        myShader.SetFloat("pointLights[3].constant", 1.0f);
        myShader.SetFloat("pointLights[3].linear", 0.09f);
        myShader.SetFloat("pointLights[3].quadratic", 0.032f);
        // spotLight
        myShader.SetVec3("spotLight.pos", m_camera.pos);
        myShader.SetVec3("spotLight.direction", m_camera.front);
        myShader.SetVec3("spotLight.ambient", 0.0f, 0.0f, 0.0f);
        myShader.SetVec3("spotLight.diffuse", 1.0f, 1.0f, 1.0f);
        myShader.SetVec3("spotLight.specular", 1.0f, 1.0f, 1.0f);
        myShader.SetFloat("spotLight.constant", 1.0f);
        myShader.SetFloat("spotLight.linear", 0.09f);
        myShader.SetFloat("spotLight.quadratic", 0.032f);
        myShader.SetFloat("spotLight.cutOff", glm::cos(glm::radians(12.5f)));
        myShader.SetFloat("spotLight.outerCutOff", glm::cos(glm::radians(15.0f)));

        // set material in shader (diffuse and specular is set as texture once above)
        myShader.SetFloat("material.shininess", 32.0f); // bind diffuse map

        // TODO remove this
        DrawModel(backpackTransform, backpack, myShader);
        DrawModel(backpack2Transform, backpack2, myShader);

        DrawLights();
    }

    void PostUpdate(Fenrir::App& app)
    {
    }

    void Exit(Fenrir::App& app)
    {
    }

    // TODO this will eventually be private and called by ecs
    void DrawModel(Transform& transform, Model& model, Shader& shader)
    {
        Fenrir::Math::Mat4 mdl_mat = Fenrir::Math::Mat4(1.0f);

        mdl_mat = Fenrir::Math::Translate(mdl_mat, transform.pos);

        mdl_mat *= Fenrir::Math::Mat4Cast(transform.rot);

        mdl_mat = Fenrir::Math::Scale(mdl_mat, transform.scale);

        myShader.SetMat4("model", mdl_mat);

        for (auto& mesh : model.meshes)
        {
            DrawMesh(mesh, shader);
        }
    }

  private:
    Fenrir::ILogger& m_logger;
    Window& m_window;
    Fenrir::Camera& m_camera;

    Fenrir::Math::Mat4 m_view;
    Fenrir::Math::Mat4 m_projection;

    // TODO remove this eww
    void DrawLights()
    {
        //! DRAWING LIGHTS
        lightShader.Use();
        lightShader.SetMat4("view", m_view);
        lightShader.SetMat4("projection", m_projection);

        glBindVertexArray(lightVAO);
        for (unsigned int i = 0; i < 4; i++)
        {
            // calculate the model matrix for each object and pass it to shader before drawing
            Fenrir::Math::Mat4 model = Fenrir::Math::Mat4(1.0f);
            model = Fenrir::Math::Translate(model, pointLightPositions[i]);
            model = glm::scale(model, glm::vec3(0.2f));
            lightShader.SetMat4("model", model);

            glDrawArrays(GL_TRIANGLES, 0, 36);
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
    AssetLoader(Fenrir::ILogger& logger)
        : m_shaderLibrary(logger), m_textureLibrary(logger), m_modelLibrary(logger, m_textureLibrary, m_shaderLibrary)
    {
    }

    void Init(Fenrir::App&)
    {
        m_shaderLibrary.AddShader("lightedObject", "assets/shaders/lighted_vertex.glsl",
                                  "assets/shaders/lighted_fragment.glsl");
        m_shaderLibrary.AddShader("light", "assets/shaders/light_vertex.glsl", "assets/shaders/light_fragment.glsl");

        myShader = m_shaderLibrary.GetShader("lightedObject");
        lightShader = m_shaderLibrary.GetShader("light");

        glActiveTexture(GL_TEXTURE0);
        cubeDiffuse = m_textureLibrary.GetTexture("assets/textures/art-deco-scales/art-deco-scales_albedo.png");

        glActiveTexture(GL_TEXTURE1);
        cubeSpecular = m_textureLibrary.GetTexture("assets/textures/art-deco-scales/art-deco-scales_metallic.png");

        m_modelLibrary.AddModel("assets/models/backpack/backpack.obj");

        backpack = m_modelLibrary.GetModel("assets/models/backpack/backpack.obj");
        backpack2 = m_modelLibrary.GetModel("assets/models/backpack/backpack.obj");
    }

  private:
    ShaderLibrary m_shaderLibrary;
    TextureLibrary m_textureLibrary;
    ModelLibrary m_modelLibrary;
};

#define BIND_ASSET_LOADER_FN(fn, assetLoaderInstance) \
    std::bind(&AssetLoader::fn, &assetLoaderInstance, std::placeholders::_1)

int main()
{
    auto logger = std::make_unique<Fenrir::ConsoleLogger>();
    Fenrir::App app(std::move(logger));

    Fenrir::Camera camera;
    Window window("Demo App");

    CameraController cameraController(camera, 0.1f, 3.0f);

    GLRenderer glRenderer(*app.Logger().get(), window, camera);

    AssetLoader assetLoader(*app.Logger().get());

    app.AddSystems(Fenrir::SchedulePriority::PreInit, {BIND_WINDOW_SYSTEM_FN(Window::PreInit, window)})
        .AddSystems(Fenrir::SchedulePriority::Init,
                    {BIND_GL_RENDERER_FN(GLRenderer::Init, glRenderer),
                     BIND_ASSET_LOADER_FN(AssetLoader::Init, assetLoader), InitCubes, InitLights})
        // .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        .AddSystems(Fenrir::SchedulePriority::PreUpdate, {BIND_GL_RENDERER_FN(GLRenderer::PreUpdate, glRenderer)})
        .AddSystems(Fenrir::SchedulePriority::Update,
                    {BIND_CAMERA_CONTROLLER_FN(CameraController::Update, cameraController),
                     BIND_GL_RENDERER_FN(GLRenderer::Update, glRenderer)})
        //    .AddSystems(Fenrir::SchedulePriority::Tick, {Tick})
        .AddSystems(Fenrir::SchedulePriority::PostUpdate, {BIND_GL_RENDERER_FN(GLRenderer::PostUpdate, glRenderer),
                                                           BIND_WINDOW_SYSTEM_FN(Window::PostUpdate, window)})
        .AddSystems(Fenrir::SchedulePriority::Exit,
                    {BIND_WINDOW_SYSTEM_FN(Window::Exit, window), BIND_GL_RENDERER_FN(GLRenderer::Exit, glRenderer)})
        .Run();
}