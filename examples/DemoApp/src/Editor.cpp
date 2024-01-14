#include "Editor.hpp"

#include <optional>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <ImGuizmo.h>

#include "firaCode.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirCamera/Camera.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirMath/Math.hpp"

#include "Events.hpp"
#include "GLRenderer.hpp"
#include "ModelLibrary.hpp"
#include "Window.hpp"

Editor::Editor(Fenrir::App& app, Fenrir::ILogger& logger, Window& window, GLRenderer& renderer, Fenrir::Camera& camera,
               TextureLibrary& textureLibrary)
    : m_app(app), m_logger(logger), m_window(window), m_renderer(renderer), m_camera(camera),
      m_textureLibrary(textureLibrary)
{
}
void Editor::SetProjectSettings(const ProjectSettings& settings)
{
    m_projectSettings = settings;
    m_projectPath = std::filesystem::path(settings.assetPath);
    m_currentPath = std::filesystem::path(settings.assetPath);

    m_projectPath = std::filesystem::canonical(m_projectPath);
    m_currentPath = std::filesystem::canonical(m_currentPath);
}

void Editor::Init(Fenrir::App&)
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

    m_fenrirIconTexture = m_textureLibrary.GetTexture(m_projectSettings.assetPath + "textures/icons/fenrir.png");
    m_fileIconTexture = m_textureLibrary.GetTexture(m_projectSettings.assetPath + "textures/icons/file.png");
    m_folderIconTexture = m_textureLibrary.GetTexture(m_projectSettings.assetPath + "textures/icons/folder.png");
}

void Editor::InitDockingLayout()
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

void Editor::PreUpdate(Fenrir::App& app)
{
    // resize the framebuffer to that of the sceneView size
    m_renderer.ResizeFrameBuffer(m_frameBuffer, static_cast<int>(m_sceneViewSize.x),
                                 static_cast<int>(m_sceneViewSize.y));

    // bind the framebuffer to draw the scene to
    m_renderer.BindFrameBuffer(m_frameBuffer);
}

void Editor::OnMouseClick(const MouseButtonEvent& event)
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

void Editor::Update(Fenrir::App& app)
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

void Editor::Exit(Fenrir::App& app)
{
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::DestroyPlatformWindows();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(m_guiContext);
}

Fenrir::Math::Vec2 Editor::ScreenToDeviceCoords(const Fenrir::Math::Vec2& screenPoint)
{
    Fenrir::Math::Vec2 adjustedPoint;
    adjustedPoint.x = screenPoint.x - m_SceneViewPos.x;
    adjustedPoint.y = screenPoint.y - m_SceneViewPos.y;
    // adjustedPoint.y = (screenPoint.y - (m_SceneViewPos.y + m_menuBarHeight));

    Fenrir::Math::Vec2 normalizedCoords;
    normalizedCoords.x = (2.0f * adjustedPoint.x) / m_sceneViewSize.x - 1.0f;
    normalizedCoords.y = 1.0f - (2.0f * adjustedPoint.y) / m_sceneViewSize.y;

    return normalizedCoords;
}

Fenrir::Math::Ray Editor::ScreenToPointRay(const Fenrir::Math::Vec2& normalisedCoords)
{
    Fenrir::Math::Vec4 clipCoords = Fenrir::Math::Vec4(normalisedCoords.x, normalisedCoords.y, -1.0f, 1.0f);

    Fenrir::Math::Mat4 projection = Fenrir::Math::Perspective(
        Fenrir::Math::DegToRad(m_camera.fov), static_cast<float>(m_sceneViewSize.x / m_sceneViewSize.y), 0.1f, 100.0f);

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

Fenrir::Entity Editor::SelectEntity(const Fenrir::Math::Ray& ray, Fenrir::EntityList& entityList)
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

void Editor::MenuBar()
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

void Editor::SceneHierarchyWindow()
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

void Editor::DisplayEntityNode(Fenrir::Entity entity)
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

void Editor::SceneViewWindow()
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

void Editor::PropertiesWindow()
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

void Editor::AssetBrowserWindow()
{
    ImGui::Begin("Asset Browser", nullptr, scene_window_flags);

    static std::optional<std::filesystem::path> selectedItem = std::nullopt;

    if (ImGui::IsWindowFocused())
    {
        ImGuiIO& io = ImGui::GetIO();

        // Check if an item is selected
        if (selectedItem.has_value()) // Assuming selectedItem is an std::optional or similar
        {
            // Cut (CTRL + X)
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_X)))
            {
                // CutFile(selectedItem.value());
            }

            // Copy (CTRL + C)
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C)))
            {
                // CopyFile(selectedItem.value());
            }

            // Paste (CTRL + V)
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
            {
                // PasteFile(m_currentPath);
            }
        }

        // Undo (CTRL + Z)
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z)))
        {
            // UndoLastCommand();
        }
    }

    if (m_currentPath.compare(m_projectPath) != 0)
    {

        if (ImGui::Button("<-"))
            m_currentPath = m_currentPath.parent_path();

        // the back button is a drag and drop target
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char* droppedPathStr = static_cast<const char*>(payload->Data);
                std::filesystem::path droppedAbsolutePath(droppedPathStr);
                std::filesystem::path parentPath = m_currentPath.parent_path();
                std::filesystem::path targetAbsolutePath = parentPath / droppedAbsolutePath.filename();

                try
                {
                    std::filesystem::rename(droppedAbsolutePath, targetAbsolutePath);
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    m_logger.Error("Editor::AssetBrowserWindow - {}", e.what());
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    static float padding = 24.0f;
    static float thumbnailSize = 64.0f;
    static float cellSize = thumbnailSize + padding;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = static_cast<int>(panelWidth / cellSize);
    if (columnCount < 1)
        columnCount = 1;

    ImGui::Columns(columnCount, nullptr, false);

    for (auto& entry : std::filesystem::directory_iterator(m_currentPath))
    {
        const auto& path = entry.path();
        const auto& filename = path.filename().string();

        ImGui::PushID(filename.c_str());
        Texture icon = path.extension().string() == ".feproj" ? m_fenrirIconTexture
                       : entry.is_directory()                 ? m_folderIconTexture
                                                              : m_fileIconTexture;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::ImageButton(reinterpret_cast<void*>(static_cast<intptr_t>(icon.Id)),
                           ImVec2(thumbnailSize, thumbnailSize), ImVec2(0, 1), ImVec2(1, 0));

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            selectedItem = path;
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("Asset Browser Context Menu");
            selectedItem = path;

            if (ImGui::BeginPopup("Asset Browser Context Menu"))
            {
                if (ImGui::MenuItem("Delete"))
                {
                    // DeleteFile(selectedItem.value());
                }
                ImGui::EndPopup();
            }
        }

        static std::string draggedPath;

        if (ImGui::BeginDragDropSource())
        {
            draggedPath = path.string();

            ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", draggedPath.c_str(), draggedPath.length() + 1);

            ImGui::Text("%s", filename.c_str());
            ImGui::EndDragDropSource();
        }

        if (entry.is_directory() && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char* droppedPathStr = static_cast<const char*>(payload->Data);
                std::filesystem::path droppedAbsolutePath(droppedPathStr);
                std::filesystem::path targetAbsolutePath = entry / droppedAbsolutePath.filename();

                try
                {
                    std::filesystem::rename(droppedAbsolutePath, targetAbsolutePath);
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    m_logger.Error("Editor::AssetBrowserWindow - {}", e.what());
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (entry.is_directory())
            {
                m_currentPath /= path.filename();
            }
        }
        ImGui::TextWrapped(filename.c_str());
        ImGui::NextColumn();
        ImGui::PopID();
    }
    ImGui::Columns(1);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        selectedItem = std::nullopt;
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        selectedItem = std::nullopt;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
    {
        selectedItem = std::nullopt;
    }

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
    {
        selectedItem = std::nullopt;
    }

    ImGui::End();
}

void Editor::ConsoleWindow()
{
    ImGui::Begin("Console", nullptr, scene_window_flags);
    ImGui::Text("Console");
    ImGui::End();
}

void Editor::DrawGuizmo()
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