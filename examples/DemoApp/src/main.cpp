
#include <string>
#include <unordered_set>

#include "CameraController.hpp"
#include "Window.hpp"

#include "ModelLibrary.hpp"
#include "ShaderLibrary.hpp"
#include "TextureLibrary.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirCamera/Camera.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirMath/Math.hpp"

#include <glad/glad.h>

struct Transform
{
    Fenrir::Math::Vec3 pos;
    Fenrir::Math::Vec3 rot; // TODO convert to quat
    Fenrir::Math::Vec3 scale;
};

Fenrir::Camera camera;
Window window("Demo App");

class GLRenderer
{
};

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

void DrawModel(Model& model, Shader& shader)
{
    for (auto& mesh : model.meshes)
    {
        DrawMesh(mesh, shader);
    }
}

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

const glm::vec3 cubePositions[10] = {glm::vec3(0.0f, 0.0f, 0.0f),    glm::vec3(2.0f, 5.0f, -15.0f),
                                     glm::vec3(-1.5f, -2.2f, -2.5f), glm::vec3(-3.8f, -2.0f, -12.3f),
                                     glm::vec3(2.4f, -0.4f, -3.5f),  glm::vec3(-1.7f, 3.0f, -7.5f),
                                     glm::vec3(1.3f, -2.0f, -2.5f),  glm::vec3(1.5f, 2.0f, -2.5f),
                                     glm::vec3(1.5f, 0.2f, -1.5f),   glm::vec3(-1.3f, 1.0f, -1.5f)};

Shader cubeShader;

Fenrir::Math::Mat4 view;
Fenrir::Math::Mat4 projection;

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

    // cubeShader.Use();

    // // setting material diffuse can be set once
    // cubeShader.SetInt("material.diffuse", 0);
    // // setting material specular can be set once
    // cubeShader.SetInt("material.specular", 1);
}

void DrawCubes(Fenrir::App& app)
{
    //! DRAWING CUBES
    cubeShader.Use();

    cubeShader.SetMat4("view", view);
    cubeShader.SetMat4("projection", projection);

    // directional light
    cubeShader.SetVec3("dirLight.direction", -0.2f, -1.0f, -0.3f);
    cubeShader.SetVec3("dirLight.ambient", 0.05f, 0.05f, 0.05f);
    cubeShader.SetVec3("dirLight.diffuse", 0.4f, 0.4f, 0.4f);
    cubeShader.SetVec3("dirLight.specular", 0.5f, 0.5f, 0.5f);
    // point light 1
    cubeShader.SetVec3("pointLights[0].pos", pointLightPositions[0]);
    cubeShader.SetVec3("pointLights[0].ambient", 0.05f, 0.05f, 0.05f);
    cubeShader.SetVec3("pointLights[0].diffuse", 0.8f, 0.8f, 0.8f);
    cubeShader.SetVec3("pointLights[0].specular", 1.0f, 1.0f, 1.0f);
    cubeShader.SetFloat("pointLights[0].constant", 1.0f);
    cubeShader.SetFloat("pointLights[0].linear", 0.09f);
    cubeShader.SetFloat("pointLights[0].quadratic", 0.032f);
    // point light 2
    cubeShader.SetVec3("pointLights[1].pos", pointLightPositions[1]);
    cubeShader.SetVec3("pointLights[1].ambient", 0.05f, 0.05f, 0.05f);
    cubeShader.SetVec3("pointLights[1].diffuse", 0.8f, 0.8f, 0.8f);
    cubeShader.SetVec3("pointLights[1].specular", 1.0f, 1.0f, 1.0f);
    cubeShader.SetFloat("pointLights[1].constant", 1.0f);
    cubeShader.SetFloat("pointLights[1].linear", 0.09f);
    cubeShader.SetFloat("pointLights[1].quadratic", 0.032f);
    // point light 3
    cubeShader.SetVec3("pointLights[2].pos", pointLightPositions[2]);
    cubeShader.SetVec3("pointLights[2].ambient", 0.05f, 0.05f, 0.05f);
    cubeShader.SetVec3("pointLights[2].diffuse", 0.8f, 0.8f, 0.8f);
    cubeShader.SetVec3("pointLights[2].specular", 1.0f, 1.0f, 1.0f);
    cubeShader.SetFloat("pointLights[2].constant", 1.0f);
    cubeShader.SetFloat("pointLights[2].linear", 0.09f);
    cubeShader.SetFloat("pointLights[2].quadratic", 0.032f);
    // point light 4
    cubeShader.SetVec3("pointLights[3].pos", pointLightPositions[3]);
    cubeShader.SetVec3("pointLights[3].ambient", 0.05f, 0.05f, 0.05f);
    cubeShader.SetVec3("pointLights[3].diffuse", 0.8f, 0.8f, 0.8f);
    cubeShader.SetVec3("pointLights[3].specular", 1.0f, 1.0f, 1.0f);
    cubeShader.SetFloat("pointLights[3].constant", 1.0f);
    cubeShader.SetFloat("pointLights[3].linear", 0.09f);
    cubeShader.SetFloat("pointLights[3].quadratic", 0.032f);
    // spotLight
    cubeShader.SetVec3("spotLight.pos", camera.pos);
    cubeShader.SetVec3("spotLight.direction", camera.front);
    cubeShader.SetVec3("spotLight.ambient", 0.0f, 0.0f, 0.0f);
    cubeShader.SetVec3("spotLight.diffuse", 1.0f, 1.0f, 1.0f);
    cubeShader.SetVec3("spotLight.specular", 1.0f, 1.0f, 1.0f);
    cubeShader.SetFloat("spotLight.constant", 1.0f);
    cubeShader.SetFloat("spotLight.linear", 0.09f);
    cubeShader.SetFloat("spotLight.quadratic", 0.032f);
    cubeShader.SetFloat("spotLight.cutOff", glm::cos(glm::radians(12.5f)));
    cubeShader.SetFloat("spotLight.outerCutOff", glm::cos(glm::radians(15.0f)));

    // set material in shader (diffuse and specular is set as texture once above)
    cubeShader.SetFloat("material.shininess", 32.0f); // bind diffuse map

    // // bind textures on corresponding texture units
    // glActiveTexture(GL_TEXTURE0);
    // glBindTexture(GL_TEXTURE_2D, cubeDiffuse.Id);
    // glActiveTexture(GL_TEXTURE1);
    // glBindTexture(GL_TEXTURE_2D, cubeSpecular.Id);

    // glBindVertexArray(cubeVAO);

    // for (unsigned int i = 0; i < 10; i++)
    // {
    //     glm::mat4 model = glm::mat4(1.0f);
    //     model = glm::translate(model, cubePositions[i]);
    //     float angle = 20.0f * i;
    //     model = glm::rotate(model, Fenrir::Math::DegToRad(angle), glm::vec3(1.0f, 0.3f, 0.5f));
    //     cubeShader.SetMat4("model", model);

    //     glDrawArrays(GL_TRIANGLES, 0, 36);
    // }
    // glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

    // glBindVertexArray(0); // dont need to unbind every time
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.0f)); // translate it down so it's at the center of the scene
    model = glm::scale(model, glm::vec3(1.0f, 1.0f, 1.0f));     // it's a bit too big for our scene, so scale it down
    cubeShader.SetMat4("model", model);

    DrawModel(backpack, cubeShader);

    model = glm::translate(model, glm::vec3(0.0f, 5.0f, 0.0f)); // translate it down so it's at the center of the scene
    model = glm::scale(model, glm::vec3(1.0f, 1.0f, 1.0f));     // it's a bit too big for our scene, so scale it down
    cubeShader.SetMat4("model", model);

    DrawModel(backpack2, cubeShader);
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

void DrawLights(Fenrir::App& app)
{
    //! DRAWING LIGHTS
    lightShader.Use();
    lightShader.SetMat4("view", view);
    lightShader.SetMat4("projection", projection);

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

void RenderInit(Fenrir::App& app)
{
    //? GL SPECIFIC CODE FOR TESTING
    glViewport(0, 0, window.GetWidth(), window.GetHeight());

    glEnable(GL_DEPTH_TEST);
}

void RenderPreUpdate(Fenrir::App& app)
{
    // glClearColor(0.45f, 0.6f, 0.75f, 1.0f); // vakol blue
    glClearColor(0.529f, 0.808f, 0.922f, 1.0f); // sky blue
    // glClearColor(0.0941176f, 0.0941176f, 0.0941176f, 1.0f); // dark grey
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fill mode
}

void RenderUpdate(Fenrir::App& app)
{
    // TODO move this eventually
    view = camera.GetViewMatrix();

    projection = Fenrir::Math::Perspective(Fenrir::Math::DegToRad(camera.fov),
                                           static_cast<float>(window.GetWidth() / window.GetHeight()), 0.1f, 100.0f);
}

void RenderExit(Fenrir::App& app)
{
    // cleanup of resources
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteVertexArrays(1, &lightVAO);

    glDeleteBuffers(1, &cubeVBO);
    glDeleteBuffers(1, &cubeEBO);
}

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

        cubeShader = m_shaderLibrary.GetShader("lightedObject");
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

    CameraController cameraController(camera, 0.1f, 3.0f);

    AssetLoader assetLoader(*app.Logger().get());

    app.AddSystems(Fenrir::SchedulePriority::PreInit, {BIND_WINDOW_SYSTEM_FN(Window::PreInit, window)})
        .AddSystems(Fenrir::SchedulePriority::Init,
                    {RenderInit, BIND_ASSET_LOADER_FN(AssetLoader::Init, assetLoader), InitCubes, InitLights})
        // .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        .AddSystems(Fenrir::SchedulePriority::PreUpdate, {RenderPreUpdate})
        .AddSystems(Fenrir::SchedulePriority::Update,
                    {BIND_CAMERA_CONTROLLER_FN(CameraController::Update, cameraController), RenderUpdate, DrawCubes,
                     DrawLights})
        // .AddSystems(Fenrir::SchedulePriority::Tick, {Tick})
        .AddSystems(Fenrir::SchedulePriority::PostUpdate, {BIND_WINDOW_SYSTEM_FN(Window::PostUpdate, window)})
        .AddSystems(Fenrir::SchedulePriority::Exit, {BIND_WINDOW_SYSTEM_FN(Window::Exit, window), RenderExit})
        .Run();
}