#pragma once

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

#include "GLRenderer.hpp"

namespace Fenrir
{
    class App;
    class ILogger;
    class Camera;
    class Entity;
    class EntityList;
} // namespace Fenrir

class Window;
class GLRenderer;
struct MouseButtonEvent;

// TODO dont include imgui, instead use forward decleration
#include <imgui.h>

class Editor
{
  public:
    Editor(Fenrir::App& app, Fenrir::ILogger& logger, Window& window, GLRenderer& renderer, Fenrir::Camera& camera);

    void Init(Fenrir::App& app);

    void InitDockingLayout();

    void PreUpdate(Fenrir::App& app);
    void OnMouseClick(const MouseButtonEvent& event);

    void Update(Fenrir::App& app);

    void Exit(Fenrir::App& app);

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

    Fenrir::Math::Vec2 ScreenToDeviceCoords(const Fenrir::Math::Vec2& screenPoint);

    Fenrir::Math::Ray ScreenToPointRay(const Fenrir::Math::Vec2& normalisedCoords);

    Fenrir::Entity SelectEntity(const Fenrir::Math::Ray& ray, Fenrir::EntityList& entityList);
    void MenuBar();
    void SceneHierarchyWindow();
    void DisplayEntityNode(Fenrir::Entity entity);
    void SceneViewWindow();
    void PropertiesWindow();
    void AssetBrowserWindow();
    void ConsoleWindow();
    void DrawGuizmo();
};

#define BIND_EDITOR_FN(fn, editorInstance) std::bind(&Editor::fn, &editorInstance, std::placeholders::_1)
