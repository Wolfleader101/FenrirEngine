#include "GLRenderer.hpp"

#include <glad/glad.h>

#include "FenrirApp/App.hpp"
#include "FenrirCamera/Camera.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirLogger/ILogger.hpp"

#include "Events.hpp"
#include "Material.hpp"
#include "ModelLibrary.hpp"
#include "Window.hpp"

static float skyboxVertices[] = {
    // positions
    -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f,
    1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f,

    -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f,
    -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,

    1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f,

    -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,

    -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,

    -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f,
    1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f};

GLRenderer::GLRenderer(Fenrir::ILogger& logger, Window& window, Fenrir::Camera& camera)
    : m_logger(logger), m_window(window), m_camera(camera)
{
}

void GLRenderer::Init(Fenrir::App& app)
{
    m_logger.Info("GLRenderer::Init - Initializing GLRenderer");

    glViewport(0, 0, m_window.GetWidth(), m_window.GetHeight());

    glEnable(GL_DEPTH_TEST);

    SetAspectRatio(static_cast<float>(m_window.GetWidth()) / static_cast<float>(m_window.GetHeight()));

    // create skybox vao based on skybox vertices

    glGenVertexArrays(1, &skyboxVAO);
    glBindVertexArray(skyboxVAO);

    unsigned int skyboxVBO = 0;
    glGenBuffers(1, &skyboxVBO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    glBindVertexArray(0);
}

void GLRenderer::PreUpdate(Fenrir::App& app)
{
    glClearColor(0.45f, 0.6f, 0.75f, 1.0f); // vakol blue
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fill mode
}

void GLRenderer::OnWindowFrameBufferResize(const WindowFrameBufferResizeEvent& event)
{
    glViewport(0, 0, event.width, event.height);
}

void GLRenderer::Update(Fenrir::App& app)
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

    if (m_skybox.name != "")
        DrawSkybox();
}

void GLRenderer::PostUpdate(Fenrir::App& app)
{
}

void GLRenderer::Exit(Fenrir::App& app)
{
}

void GLRenderer::SetAspectRatio(float aspectRatio)
{
    m_aspecRatio = aspectRatio;
    m_projection = Fenrir::Math::Perspective(Fenrir::Math::DegToRad(m_camera.fov), m_aspecRatio, 0.1f, 100.0f);
}

GLRenderer::Framebuffer GLRenderer::CreateFrameBuffer(int width, int height)
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

void GLRenderer::DeleteFrameBuffer(unsigned int fbo)
{
    glDeleteFramebuffers(1, &fbo);
}

void GLRenderer::BindFrameBuffer(const Framebuffer& frameBuffer)
{
    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer.fbo);
    glViewport(0, 0, frameBuffer.width, frameBuffer.height);
}

void GLRenderer::UnbindFrameBuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SetAspectRatio(static_cast<float>(m_window.GetWidth()) / static_cast<float>(m_window.GetHeight()));
    glViewport(0, 0, m_window.GetWidth(), m_window.GetHeight());
}

void GLRenderer::ResizeFrameBuffer(Framebuffer& frameBuffer, int width, int height)
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

Fenrir::Math::Mat4 GLRenderer::GetProjectionMatrix() const
{
    return m_projection;
}

void GLRenderer::SetSkybox(Skybox& skybox)
{
    m_skybox = skybox;
}

void GLRenderer::SetSkyboxShader(Shader& shader)
{
    m_skyboxShader = shader;
}

void GLRenderer::SetMatProps(const Shader& shader, const Material& mat)
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

void GLRenderer::DrawModel(const Fenrir::Transform& transform, const Fenrir::Relationship& relationship,
                           const Model& model, const Shader& shader)
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

    // DrawAABB(model, mdl_mat);
}

void GLRenderer::DrawMesh(const Mesh& mesh, const Shader& shader)
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

// void GLRenderer::DrawAABB(const Model& model, const Fenrir::Math::Mat4& modelMatrix)
// {
//     std::vector<Fenrir::Math::Vec3> vertices = {
//         // 8 corners of the AABB
//         model.boundingBox.min,
//         Fenrir::Math::Vec3(model.boundingBox.max.x, model.boundingBox.min.y, model.boundingBox.min.z),
//         Fenrir::Math::Vec3(model.boundingBox.min.x, model.boundingBox.max.y, model.boundingBox.min.z),
//         Fenrir::Math::Vec3(model.boundingBox.max.x, model.boundingBox.max.y, model.boundingBox.min.z),
//         Fenrir::Math::Vec3(model.boundingBox.min.x, model.boundingBox.min.y, model.boundingBox.max.z),
//         Fenrir::Math::Vec3(model.boundingBox.max.x, model.boundingBox.min.y, model.boundingBox.max.z),
//         Fenrir::Math::Vec3(model.boundingBox.min.x, model.boundingBox.max.y, model.boundingBox.max.z),
//         model.boundingBox.max};

//     // AABB lines
//     std::vector<unsigned int> indices = {0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 3, 7, 2, 6};

//     // Create and bind VAO, VBO, and EBO
//     unsigned int VAO, VBO, EBO;
//     glGenVertexArrays(1, &VAO);
//     glGenBuffers(1, &VBO);
//     glGenBuffers(1, &EBO);

//     glBindVertexArray(VAO);
//     glBindBuffer(GL_ARRAY_BUFFER, VBO);
//     glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Fenrir::Math::Vec3), &vertices[0], GL_STATIC_DRAW);
//     glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
//     glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

//     // Position attribute
//     glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Fenrir::Math::Vec3), (void*)0);
//     glEnableVertexAttribArray(0);

//     // Use your AABB shader here
//     aabbShader.Use();
//     aabbShader.SetMat4("model", modelMatrix); // Convert Transform to model matrix
//     aabbShader.SetMat4("view", m_view);
//     aabbShader.SetMat4("projection", m_projection);
//     aabbShader.SetVec3("color", Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f)); // Red color for AABB

//     // Draw the AABB
//     glBindVertexArray(VAO);
//     glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_INT, 0);

//     // Cleanup
//     glBindVertexArray(0);
//     glDeleteVertexArrays(1, &VAO);
//     glDeleteBuffers(1, &VBO);
//     glDeleteBuffers(1, &EBO);
// }

void GLRenderer::DrawSkybox()
{
    glDepthFunc(GL_LEQUAL);
    m_skyboxShader.Use();
    auto skyBoxView = Fenrir::Math::Mat4(Fenrir::Math::Mat3(m_view));

    m_skyboxShader.SetMat4("view", skyBoxView);
    m_skyboxShader.SetMat4("projection", m_projection);

    glBindVertexArray(skyboxVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_skybox.Id);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}