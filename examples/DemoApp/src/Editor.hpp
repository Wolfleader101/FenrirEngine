#pragma once

#include <filesystem>

#include "FenrirECS/Entity.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirMath/Math.hpp"

#include "GLRenderer.hpp"
#include "TextureLibrary.hpp"

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

// TODO move this to a project settings file
struct ProjectSettings
{
    std::string identity = "";
    std::string name = "New Project";
    std::string version = "0.1.0";
    std::string description = "";
    std::string author = "";
    std::string assetPath = "";
};

class EditorConsoleLogger : public Fenrir::ILogger
{
  public:
    EditorConsoleLogger(Fenrir::ILogger& logger) = default;

  protected:
    void LogImpl(const std::string& message) override;
    void InfoImpl(const std::string& message) override;
    void WarnImpl(const std::string& message) override;
    void ErrorImpl(const std::string& message) override;
    void FatalImpl(const std::string& message) override;
};

class Editor
{
  public:
    Editor(Fenrir::App& app, Fenrir::ILogger& logger, Window& window, GLRenderer& renderer, Fenrir::Camera& camera,
           TextureLibrary& textureLibrary);

    void Init(Fenrir::App& app);

    void InitDockingLayout();

    void PreUpdate(Fenrir::App& app);
    void OnMouseClick(const MouseButtonEvent& event);

    void Update(Fenrir::App& app);

    void Exit(Fenrir::App& app);

    void SetProjectSettings(const ProjectSettings& settings);

  private:
    Fenrir::App& m_app;
    Fenrir::ILogger& m_logger;
    Window& m_window;
    GLRenderer& m_renderer;
    Fenrir::Camera& m_camera;
    TextureLibrary& m_textureLibrary;
    ProjectSettings m_projectSettings;
    std::filesystem::path m_projectPath;
    std::filesystem::path m_currentPath;

    Texture m_fileIconTexture;
    Texture m_folderIconTexture;
    Texture m_fenrirIconTexture;
    ImVec2 m_sceneViewSize = ImVec2(1.0f, 1.0f);
    ImVec2 m_SceneViewPos;
    float m_menuBarHeight = 0.0f;

    ImGuiContext* m_guiContext;
    GLRenderer::Framebuffer m_frameBuffer;
    Fenrir::Entity m_selectedEntity;

    std::shared_ptr<EditorConsoleLogger> m_consoleLogger;

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

    void PasteFile(const std::filesystem::path& targetDir);
};

#define BIND_EDITOR_FN(fn, editorInstance) std::bind(&Editor::fn, &editorInstance, std::placeholders::_1)
