#pragma once

#include <span>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirMath/Math.hpp"
#include "ShaderLibrary.hpp"
#include "TextureLibrary.hpp"

namespace Fenrir
{
    class App;
    class ILogger;
    class Camera;
    class EntityList;
    struct Transform;
    struct Relationship;
} // namespace Fenrir

class Window;
struct WindowFrameBufferResizeEvent;
class Shader;
class Material;
class Model;
class Mesh;

// One end of a line drawn by GLRenderer::DrawLines, in world space.
struct LineVertex
{
    Fenrir::Math::Vec3 pos;
    Fenrir::Math::Vec3 colour;
};

class GLRenderer
{
  public:
    // TODO get rid of m_window depedency
    GLRenderer(Fenrir::ILogger& logger, Window& window, Fenrir::Camera& camera);

    void Init(Fenrir::App& app);

    void PreUpdate(Fenrir::App& app);

    void OnWindowFrameBufferResize(const WindowFrameBufferResizeEvent& event);

    void Update(Fenrir::App& app);

    void PostUpdate(Fenrir::App& app);

    void Exit(Fenrir::App& app);

    void SetAspectRatio(float aspectRatio);

    Fenrir::Math::Mat4 GetProjectionMatrix() const;

    struct Framebuffer
    {
        unsigned int fbo;
        unsigned int texture;
        unsigned int rbo;
        int width;
        int height;
    };

    Framebuffer CreateFrameBuffer(int width, int height);
    void DeleteFrameBuffer(unsigned int fbo);

    void BindFrameBuffer(const Framebuffer& frameBuffer);
    void UnbindFrameBuffer();
    void ResizeFrameBuffer(Framebuffer& frameBuffer, int width, int height);

    void SetSkybox(Skybox& skybox);

    void SetSkyboxShader(Shader& shader);

    // The shader DrawLines uses: world-space positions and a colour per vertex.
    void SetLineShader(Shader& shader);

    // Draws lines, two vertices each, in one GL_LINES pass with the current view, over everything already drawn (no
    // depth test). Call it between Update and the editor's Update, while the scene's framebuffer is bound.
    void DrawLines(std::span<const LineVertex> vertices);

  private:
    Fenrir::ILogger& m_logger;
    Window& m_window;
    Fenrir::Camera& m_camera;

    Fenrir::Math::Mat4 m_view;
    Fenrir::Math::Mat4 m_projection;
    Skybox m_skybox;
    Shader m_skyboxShader;
    float m_aspecRatio;
    unsigned int skyboxVAO;
    Shader m_lineShader;
    unsigned int m_linesVAO = 0;
    unsigned int m_linesVBO = 0;
    Fenrir::EventReader<WindowFrameBufferResizeEvent> m_resizeReader;

    void SetMatProps(const Shader& shader, const Material& mat);

    void DrawModel(const Fenrir::Math::Mat4& worldMatrix, const Model& model, const Shader& shader);

    void DrawMesh(const Mesh& mesh, const Shader& shader);

    void DrawSkybox();
};

#define BIND_GL_RENDERER_FN(fn, glRendererInstance) \
    std::bind(&GLRenderer::fn, &glRendererInstance, std::placeholders::_1)
