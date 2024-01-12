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
#include <imgui_internal.h>

#include <ImGuizmo.h>

#include "firaCode.hpp"

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

using MaterialProperty = std::variant<bool, int, float, Fenrir::Math::Vec2, Fenrir::Math::Vec3, Fenrir::Math::Vec4,
                                      Fenrir::Math::Mat3, Fenrir::Math::Mat4>;

struct Material
{
    Shader shader;
    entt::dense_map<std::string, MaterialProperty> properties;
};

Shader myShader;
Shader lightShader;
Shader aabbShader;

Model backpack;

Model cube;

const glm::vec3 pointLightPositions[4] = {glm::vec3(0.7f, 0.2f, 2.0f), glm::vec3(2.3f, -3.3f, -4.0f),
                                          glm::vec3(-4.0f, 2.0f, -12.0f), glm::vec3(0.0f, 0.0f, -3.0f)};

Fenrir::Math::Mat4 TransformToMat4(const Fenrir::Transform& transform)
{
    Fenrir::Math::Mat4 mdl_mat = Fenrir::Math::Mat4(1.0f);

    mdl_mat = Fenrir::Math::Translate(mdl_mat, transform.pos);

    mdl_mat *= Fenrir::Math::Mat4Cast(transform.rot);

    mdl_mat = Fenrir::Math::Scale(mdl_mat, transform.scale);

    return mdl_mat;
}

void InitLights(Fenrir::App& app)
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

void InitBackpacks(Fenrir::App& app)
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

class GLRenderer
{
  public:
    // TODO get rid of m_window depedency
    GLRenderer(Fenrir::ILogger& logger, Window& window, Fenrir::Camera& camera)
        : m_logger(logger), m_window(window), m_camera(camera)
    {
    }

    void Init(Fenrir::App& app)
    {
        m_logger.Info("GLRenderer::Init - Initializing GLRenderer");

        glViewport(0, 0, m_window.GetWidth(), m_window.GetHeight());

        glEnable(GL_DEPTH_TEST);

        SetAspectRatio(m_window.GetWidth() / m_window.GetHeight());
    }

    void PreUpdate(Fenrir::App& app)
    {
        glClearColor(0.45f, 0.6f, 0.75f, 1.0f); // vakol blue
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe mode
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fill mode
    }

    void OnWindowFrameBufferResize(const WindowFrameBufferResizeEvent& event)
    {
        glViewport(0, 0, event.width, event.height);
    }

    void Update(Fenrir::App& app)
    {
        for (const auto& event : app.ReadEvents<WindowFrameBufferResizeEvent>())
        {
            OnWindowFrameBufferResize(event);
        }

        m_view = m_camera.GetViewMatrix();

        // TODO above needs to be moved somehow

        Fenrir::EntityList& entityList = app.GetActiveScene().GetEntityList();

        entityList.ForEach<Fenrir::Transform, Fenrir::Relationship, Model, Material>(
            [&](Fenrir::Transform& transform, Fenrir::Relationship& relationship, Model& model, Material& material) {
                material.properties["spotLight.pos"] = m_camera.pos;
                material.properties["spotLight.direction"] = m_camera.front;
                SetMatProps(material.shader, material);
                DrawModel(transform, relationship, model, material.shader);
            });
    }

    void PostUpdate(Fenrir::App& app)
    {
    }

    void Exit(Fenrir::App& app)
    {
    }

    void SetAspectRatio(float aspectRatio)
    {
        m_aspecRatio = aspectRatio;
        m_projection = Fenrir::Math::Perspective(Fenrir::Math::DegToRad(m_camera.fov), m_aspecRatio, 0.1f, 100.0f);
    }

    struct Framebuffer
    {
        unsigned int fbo;
        unsigned int texture;
        unsigned int rbo;
        int width;
        int height;
    };

    Framebuffer CreateFrameBuffer(int width, int height)
    {
        Framebuffer frameBuffer;
        frameBuffer.width = width;
        frameBuffer.height = height;

        glGenFramebuffers(1, &frameBuffer.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer.fbo);

        glGenTextures(1, &frameBuffer.texture);
        glBindTexture(GL_TEXTURE_2D, frameBuffer.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frameBuffer.texture, 0);

        glGenRenderbuffers(1, &frameBuffer.rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer.rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, frameBuffer.rbo);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            m_logger.Fatal("GLRenderer::CreateFrameBuffer - Framebuffer is not complete!");
        }

        glBindTexture(GL_TEXTURE_2D, 0);        // unbind texture
        glBindRenderbuffer(GL_RENDERBUFFER, 0); // unbind renderbuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);   // unbind framebuffer

        return frameBuffer;
    }

    void DeleteFrameBuffer(unsigned int fbo)
    {
        glDeleteFramebuffers(1, &fbo);
    }

    void BindFrameBuffer(const Framebuffer& frameBuffer)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer.fbo);
        glViewport(0, 0, frameBuffer.width, frameBuffer.height);
    }

    void UnbindFrameBuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        SetAspectRatio(static_cast<float>(m_window.GetWidth()) / static_cast<float>(m_window.GetHeight()));
        glViewport(0, 0, m_window.GetWidth(), m_window.GetHeight());
    }

    void ResizeFrameBuffer(Framebuffer& frameBuffer, int width, int height)
    {
        if (frameBuffer.width == width && frameBuffer.height == height)
            return;

        frameBuffer.width = width;
        frameBuffer.height = height;

        glBindTexture(GL_TEXTURE_2D, frameBuffer.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frameBuffer.texture, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer.rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, frameBuffer.fbo);

        SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            m_logger.Fatal("GLRenderer::ResizeFrameBuffer - Framebuffer is not complete!");
        }

        glBindTexture(GL_TEXTURE_2D, 0);        // unbind texture
        glBindRenderbuffer(GL_RENDERBUFFER, 0); // unbind renderbuffer
    }

    Fenrir::Math::Mat4 GetProjectionMatrix() const
    {
        return m_projection;
    }

  private:
    Fenrir::ILogger& m_logger;
    Window& m_window;
    Fenrir::Camera& m_camera;

    Fenrir::Math::Mat4 m_view;
    Fenrir::Math::Mat4 m_projection;
    float m_aspecRatio;

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

    void DrawModel(const Fenrir::Transform& transform, const Fenrir::Relationship& relationship, const Model& model,
                   const Shader& shader)
    {
        Fenrir::Transform tempTran = transform;
        if (relationship.parent.IsValid())
        {
            Fenrir::Transform& parentTransform = relationship.parent.GetComponent<Fenrir::Transform>();
            tempTran.pos += parentTransform.pos;
            tempTran.rot *= parentTransform.rot;
            tempTran.scale *= parentTransform.scale;
        }
        Fenrir::Math::Mat4 mdl_mat = TransformToMat4(tempTran);

        shader.Use();
        shader.SetMat4("view", m_view);
        shader.SetMat4("projection", m_projection);
        shader.SetMat4("model", mdl_mat);

        for (const auto& mesh : model.meshes)
        {
            DrawMesh(mesh, shader);
        }

        DrawAABB(model, mdl_mat);
    }

    void DrawMesh(const Mesh& mesh, const Shader& shader)
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

    void DrawAABB(const Model& model, const Fenrir::Math::Mat4& modelMatrix)
    {
        std::vector<Fenrir::Math::Vec3> vertices = {
            // 8 corners of the AABB
            model.boundingBox.min,
            Fenrir::Math::Vec3(model.boundingBox.max.x, model.boundingBox.min.y, model.boundingBox.min.z),
            Fenrir::Math::Vec3(model.boundingBox.min.x, model.boundingBox.max.y, model.boundingBox.min.z),
            Fenrir::Math::Vec3(model.boundingBox.max.x, model.boundingBox.max.y, model.boundingBox.min.z),
            Fenrir::Math::Vec3(model.boundingBox.min.x, model.boundingBox.min.y, model.boundingBox.max.z),
            Fenrir::Math::Vec3(model.boundingBox.max.x, model.boundingBox.min.y, model.boundingBox.max.z),
            Fenrir::Math::Vec3(model.boundingBox.min.x, model.boundingBox.max.y, model.boundingBox.max.z),
            model.boundingBox.max};

        // AABB lines
        std::vector<unsigned int> indices = {0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 3, 7, 2, 6};

        // Create and bind VAO, VBO, and EBO
        unsigned int VAO, VBO, EBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Fenrir::Math::Vec3), &vertices[0], GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        // Position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Fenrir::Math::Vec3), (void*)0);
        glEnableVertexAttribArray(0);

        // Use your AABB shader here
        aabbShader.Use();
        aabbShader.SetMat4("model", modelMatrix); // Convert Transform to model matrix
        aabbShader.SetMat4("view", m_view);
        aabbShader.SetMat4("projection", m_projection);
        aabbShader.SetVec3("color", Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f)); // Red color for AABB

        // Draw the AABB
        glBindVertexArray(VAO);
        glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_INT, 0);

        // Cleanup
        glBindVertexArray(0);
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteBuffers(1, &EBO);
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
        m_shaderLibrary.AddShader("debug", m_assetPath + "shaders/debug_vertex.glsl",
                                  m_assetPath + "shaders/debug_fragment.glsl");

        myShader = m_shaderLibrary.GetShader("lightedObject");
        lightShader = m_shaderLibrary.GetShader("light");
        aabbShader = m_shaderLibrary.GetShader("debug");

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
    Editor(Fenrir::App& app, Fenrir::ILogger& logger, Window& window, GLRenderer& renderer, Fenrir::Camera& camera)
        : m_app(app), m_logger(logger), m_window(window), m_renderer(renderer), m_camera(camera)
    {
    }

    void Init(Fenrir::App& app)
    {
        m_logger.Info("Editor::Init - Initializing Editor");

        // setup imgui
        IMGUI_CHECKVERSION();
        m_guiContext = ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // enable Multi-viewport

        io.Fonts->AddFontFromMemoryCompressedBase85TTF(FiraCode_compressed_data_base85, 16.0f, nullptr,
                                                       io.Fonts->GetGlyphRangesDefault());

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.WindowBorderSize = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        ImGui_ImplGlfw_InitForOpenGL(m_window.GetGlWindow(), true); // takes in the GLFW Window
        ImGui_ImplOpenGL3_Init("#version 460");                     // sets the version of GLSL being used

        m_frameBuffer = m_renderer.CreateFrameBuffer(m_window.GetWidth(), m_window.GetHeight());
    }

    void InitDockingLayout()
    {
        static bool dockspace_initialized = false;
        if (dockspace_initialized)
            return;

        // define the docking layout
        ImGuiID dockspace_id = ImGui::GetID("FenrirDockSpace");
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspace_id);                            // clear out existing layout
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace); // add empty node
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        // split the dockspace into two nodes
        ImGuiID dock_main_id = dockspace_id; // main dock space
        ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
        ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.2f, nullptr, &dock_main_id);
        ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.2f, nullptr, &dock_main_id);

        // Dock windows into the nodes
        ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left_id);
        ImGui::DockBuilderDockWindow("Scene View", dock_main_id);
        ImGui::DockBuilderDockWindow("Properties", dock_right_id);
        ImGui::DockBuilderDockWindow("Asset Browser", dock_bottom_id);
        ImGui::DockBuilderDockWindow("Console", dock_bottom_id);

        ImGui::DockBuilderFinish(dockspace_id);

        dockspace_initialized = true;
    }

    void PreUpdate(Fenrir::App& app)
    {
        // resize the framebuffer to that of the sceneView size
        m_renderer.ResizeFrameBuffer(m_frameBuffer, static_cast<int>(m_sceneViewSize.x),
                                     static_cast<int>(m_sceneViewSize.y));

        // bind the framebuffer to draw the scene to
        m_renderer.BindFrameBuffer(m_frameBuffer);
    }

    void OnMouseClick(const MouseButtonEvent& event)
    {
        if (event.button == MouseButton::Left && event.state == InputState::Pressed)
        {
            Fenrir::EntityList& entityList = m_app.GetActiveScene().GetEntityList();

            Fenrir::Math::Vec2 screenPoint = Fenrir::Math::Vec2(event.x, event.y);

            Fenrir::Math::Vec2 normalisedCoords = ScreenToDeviceCoords(screenPoint);

            Fenrir::Math::Ray ray = ScreenToPointRay(normalisedCoords);

            Fenrir::Entity clickedEntity = SelectEntity(ray, entityList);

            if (clickedEntity.IsValid())
            {
                m_selectedEntity = clickedEntity;
            }
        }
    }

    void Update(Fenrir::App& app)
    {
        for (const auto& event : app.ReadEvents<MouseButtonEvent>())
        {
            OnMouseClick(event);
        }

        // unbind the framebuffer to draw the imgui windows
        m_renderer.UnbindFrameBuffer();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        MenuBar();

        ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(main_viewport->ID);

        ImVec2 dockspace_pos = ImVec2(0.0f, m_menuBarHeight);
        ImVec2 dockspace_size = ImVec2(main_viewport->Size.x, main_viewport->Size.y - m_menuBarHeight);
        ImGui::SetNextWindowPos(dockspace_pos);
        ImGui::SetNextWindowSize(dockspace_size);

        ImGui::Begin("FenrirDockSpace", nullptr, base_window_flags);

        InitDockingLayout();
        ImGuiID dockspace_id = ImGui::GetID("FenrirDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

        ImGui::End();

        SceneHierarchyWindow();

        SceneViewWindow();

        PropertiesWindow();

        AssetBrowserWindow();

        ConsoleWindow();

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

    void PostUpdate(Fenrir::App& app)
    {
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
    Fenrir::App& m_app;
    Fenrir::ILogger& m_logger;
    Window& m_window;
    GLRenderer& m_renderer;
    Fenrir::Camera& m_camera;
    ImVec2 m_sceneViewSize = ImVec2(1.0f, 1.0f);
    ImVec2 m_SceneViewPos;
    float m_menuBarHeight = 0.0f;

    ImGuiContext* m_guiContext;
    GLRenderer::Framebuffer m_frameBuffer;
    Fenrir::Entity m_selectedEntity;

    static const ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    static const ImGuiWindowFlags base_window_flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    static const ImGuiWindowFlags scene_window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    Fenrir::Math::Vec2 ScreenToDeviceCoords(const Fenrir::Math::Vec2& screenPoint)
    {
        Fenrir::Math::Vec2 pointRelativeToSceneView;
        pointRelativeToSceneView.x = (screenPoint.x - m_SceneViewPos.x) / m_sceneViewSize.x;
        pointRelativeToSceneView.y = (screenPoint.y - (m_SceneViewPos.y + m_menuBarHeight)) / m_sceneViewSize.y;

        // Convert to NDC [-1, 1]
        Fenrir::Math::Vec2 ndc;
        ndc.x = pointRelativeToSceneView.x * 2.0f - 1.0f;
        ndc.y = pointRelativeToSceneView.y * 2.0f - 1.0f;

        // inver the Y axis for gl as bottom-left is (-1, -1),
        ndc.y = -ndc.y;

        return ndc;
    }

    Fenrir::Math::Ray ScreenToPointRay(const Fenrir::Math::Vec2& normalisedCoords)
    {
        Fenrir::Math::Vec4 clipCoords = Fenrir::Math::Vec4(normalisedCoords.x, normalisedCoords.y, -1.0f, 1.0f);

        Fenrir::Math::Mat4 projection =
            Fenrir::Math::Perspective(Fenrir::Math::DegToRad(m_camera.fov),
                                      static_cast<float>(m_window.GetWidth() / m_window.GetHeight()), 0.1f, 100.0f);

        Fenrir::Math::Mat4 invProjection = Fenrir::Math::Inverse(projection);

        Fenrir::Math::Vec4 eyeCoords = invProjection * clipCoords;

        eyeCoords = Fenrir::Math::Vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);

        Fenrir::Math::Mat4 view = m_camera.GetViewMatrix();
        Fenrir::Math::Mat4 invView = Fenrir::Math::Inverse(view);

        Fenrir::Math::Vec4 worldCoords = invView * eyeCoords;

        Fenrir::Math::Vec3 rayDir =
            Fenrir::Math::Normalized(Fenrir::Math::Vec3(worldCoords.x, worldCoords.y, worldCoords.z));

        return Fenrir::Math::Ray(m_camera.pos, rayDir);
    }

    Fenrir::Entity SelectEntity(const Fenrir::Math::Ray& ray, Fenrir::EntityList& entityList)
    {
        Fenrir::Entity selectedEntity;
        float closestDistance = std::numeric_limits<float>::max();

        entityList.ForEachEntity([&](auto handle) {
            Fenrir::Entity entity = entityList.GetEntity(static_cast<uint32_t>(handle));
            auto& transform = entity.GetComponent<Fenrir::Transform>();

            if (entity.HasComponent<Model>())
            {
                auto& model = entity.GetComponent<Model>();

                Fenrir::Math::Mat4 mdl_mat = TransformToMat4(transform);
                if (Fenrir::Math::RayAABBIntersect(ray, model.boundingBox, mdl_mat))
                {
                    float distance = Fenrir::Math::CalculateDistance(ray.origin, model.boundingBox, mdl_mat);
                    if (distance < closestDistance)
                    {
                        closestDistance = distance;
                        selectedEntity = entity;
                    }
                }
            }
        });

        return selectedEntity;
    }

    void MenuBar()
    {
        ImGui::BeginMainMenuBar();

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene"))
            {
                // m_app.NewScene();
            }

            if (ImGui::MenuItem("Open Scene"))
            {
                // m_app.OpenScene();
            }

            if (ImGui::MenuItem("Save Scene"))
            {
                // m_app.SaveScene();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            ImGui::EndMenu();
        }
        m_menuBarHeight = ImGui::GetWindowSize().y;
        ImGui::EndMainMenuBar();
    }

    void SceneHierarchyWindow()
    {
        ImGui::Begin("Scene Hierarchy", nullptr, scene_window_flags);
        Fenrir::EntityList& entityList = m_app.GetActiveScene().GetEntityList();
        m_app.GetActiveScene().GetEntityList().ForEachEntity([&](auto handle) {
            Fenrir::Entity entity = entityList.GetEntity(static_cast<uint32_t>(handle));
            Fenrir::Relationship& relationship = entity.GetComponent<Fenrir::Relationship>();

            if (!relationship.parent.IsValid())
            {
                DisplayEntityNode(entity);
            }
        });

        ImGui::End();
    }

    void DisplayEntityNode(Fenrir::Entity entity)
    {
        auto& name = entity.GetComponent<Fenrir::Name>();
        bool isSelected = (entity == m_selectedEntity);
        if (ImGui::Selectable(name.Get(), isSelected))
        {
            m_selectedEntity = entity;
        }

        if (entity.HasComponent<Fenrir::Relationship>())
        {
            ImGui::Indent();
            entity.ForEachChild([&](Fenrir::Entity child) { DisplayEntityNode(child); });
            ImGui::Unindent();
        }
    }

    void SceneViewWindow()
    {
        ImGui::Begin("Scene View", nullptr, scene_window_flags);

        m_SceneViewPos = ImGui::GetCursorScreenPos();
        m_sceneViewSize = ImGui::GetWindowSize();

        ImGui::GetWindowDrawList()->AddImage(
            reinterpret_cast<void*>(static_cast<intptr_t>(m_frameBuffer.texture)), m_SceneViewPos,
            ImVec2(m_SceneViewPos.x + m_sceneViewSize.x, m_SceneViewPos.y + m_sceneViewSize.y), ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));

        ImGuizmo::SetDrawlist(ImGui::GetCurrentWindow()->DrawList);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(m_SceneViewPos.x, m_SceneViewPos.y, m_sceneViewSize.x, m_sceneViewSize.y);

        DrawGuizmo();

        ImGui::End();
    }

    void PropertiesWindow()
    {
        ImGui::Begin("Properties", nullptr, scene_window_flags);
        if (m_selectedEntity.IsValid())
        {
            auto& name = m_selectedEntity.GetComponent<Fenrir::Name>();
            ImGui::Text("Name: %s", name.Get());

            if (m_selectedEntity.HasComponent<Fenrir::Transform>())
            {
                auto& transform = m_selectedEntity.GetComponent<Fenrir::Transform>();
                ImGui::Text("Transform");
                ImGui::DragFloat3("Position", &transform.pos.x, 0.5f);

                static bool flipped = false;
                static uint32_t lastEntityID = Fenrir::Entity::Null;

                if (m_selectedEntity.GetId() != lastEntityID)
                {
                    lastEntityID = m_selectedEntity.GetId();
                    flipped = false;
                }

                float rotationStep = flipped ? -0.5f : 0.5f;

                Fenrir::Math::Vec3 euler = Fenrir::Math::RadToDeg(Fenrir::Math::EulerFromQuat(transform.rot));

                if (ImGui::DragFloat3("Rotation", &euler.x, rotationStep))
                {
                    // flip if pitch passes through 90 degrees
                    if (euler.y >= 90.0f || euler.y <= -90.0f)
                    {
                        flipped = !flipped;
                    }

                    transform.rot = Fenrir::Math::Normalized(Fenrir::Math::Quat(Fenrir::Math::DegToRad(euler)));
                }

                ImGui::DragFloat3("Scale", &transform.scale.x, 0.5f);
            }
        }
        ImGui::End();
    }

    void AssetBrowserWindow()
    {
        ImGui::Begin("Asset Browser", nullptr, scene_window_flags);
        ImGui::Text("Assets");
        ImGui::End();
    }

    void ConsoleWindow()
    {
        ImGui::Begin("Console", nullptr, scene_window_flags);
        ImGui::Text("Console");
        ImGui::End();
    }

    void DrawGuizmo()
    {
        if (m_selectedEntity.IsValid())
        {
            auto& transform = m_selectedEntity.GetComponent<Fenrir::Transform>();
            static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
            static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);
            static bool useSnap = false;
            static float snap[3] = {1.f, 1.f, 1.f};
            static float bounds[] = {-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
            static float boundsSnap[] = {0.1f, 0.1f, 0.1f};
            static bool boundSizing = false;
            static bool boundSizingSnap = false;

            if (ImGui::IsKeyPressed(ImGuiKey_W))
                mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E))
                mCurrentGizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R))
                mCurrentGizmoOperation = ImGuizmo::SCALE;

            auto projection = m_renderer.GetProjectionMatrix();

            Fenrir::Math::Mat4 mdl_mat = TransformToMat4(transform);

            float* mat = Fenrir::Math::AsArray(mdl_mat);

            ImGuizmo::Manipulate(Fenrir::Math::AsArray(m_camera.GetViewMatrix()), Fenrir::Math::AsArray(projection),
                                 mCurrentGizmoOperation, mCurrentGizmoMode, mat, nullptr, useSnap ? &snap[0] : nullptr,
                                 boundSizing ? bounds : nullptr, boundSizingSnap ? boundsSnap : nullptr);

            Fenrir::Math::Mat4 newMat = Fenrir::Math::MakeMat4(mat);

            Fenrir::Math::Decompose(newMat, transform.pos, transform.rot, transform.scale);
        }
    }
};

#define BIND_EDITOR_FN(fn, editorInstance) std::bind(&Editor::fn, &editorInstance, std::placeholders::_1)

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