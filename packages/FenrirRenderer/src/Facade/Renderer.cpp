#include "FenrirRenderer/Renderer.hpp"

#include <array>
#include <cmath>
#include <span>
#include <string>
#include <utility>

#include "Backend/OpenGL/OpenGLDevice.hpp"
#include "Core/BuiltinShaders.hpp"
#include "Facade/RendererState.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirWindow/Window.hpp"

namespace Fenrir
{
    namespace
    {
        using Render::AssetKey;
        using Render::KeyOf;

        /** @brief What the Background pass draws until the sky (renderer ticket 15): a dark grey, linear. */
        constexpr Colour BackgroundColour{0.015f, 0.015f, 0.015f, 1.0f};

        template <typename T>
        AssetHandle<T> HandleOf(AssetKey key)
        {
            return {key.index, key.generation};
        }

        // The public modes mirror the device's, in the same order.
        static_assert(static_cast<int>(Vsync::Off) == static_cast<int>(Graphics::Vsync::Off) &&
                      static_cast<int>(Vsync::On) == static_cast<int>(Graphics::Vsync::On) &&
                      static_cast<int>(Vsync::Adaptive) == static_cast<int>(Graphics::Vsync::Adaptive));

        Graphics::Vsync ToDevice(Vsync mode)
        {
            return static_cast<Graphics::Vsync>(mode);
        }

        Vsync FromDevice(Graphics::Vsync mode)
        {
            return static_cast<Vsync>(mode);
        }

        /** @brief Whether the shader's GPU side is ready: prepared now if it is new or changed. False if stale. */
        bool PrepareShader(Render::SceneRenderer& scene, const AssetStore& store, AssetHandle<Shader> handle)
        {
            const ShaderData* data = store.TryGet(handle);
            if (data == nullptr)
            {
                return false;
            }
            const std::uint32_t revision = store.Revision(handle);
            if (!scene.IsShaderCurrent(KeyOf(handle), revision))
            {
                scene.PrepareShader(KeyOf(handle), revision, *data);
            }
            return true;
        }

        bool PrepareMesh(Render::SceneRenderer& scene, const AssetStore& store, AssetHandle<Mesh> handle)
        {
            const MeshData* data = store.TryGet(handle);
            if (data == nullptr)
            {
                return false;
            }
            const std::uint32_t revision = store.Revision(handle);
            if (!scene.IsMeshCurrent(KeyOf(handle), revision))
            {
                scene.PrepareMesh(KeyOf(handle), revision, *data);
            }
            return true;
        }

        bool PrepareMaterial(Render::SceneRenderer& scene, const AssetStore& store, AssetHandle<Material> handle)
        {
            const MaterialData* data = store.TryGet(handle);
            if (data == nullptr)
            {
                return false;
            }
            // Its shader first: the material's block is laid out from the shader's reflection.
            PrepareShader(scene, store, data->shader);
            const std::uint32_t revision = store.Revision(handle);
            if (!scene.IsMaterialCurrent(KeyOf(handle), revision))
            {
                scene.PrepareMaterial(KeyOf(handle), revision, *data);
            }
            return true;
        }

        /** @brief Frees the GPU side of every asset whose handle went stale (ADR-0023). */
        void ReleaseStale(Render::SceneRenderer& scene, const AssetStore& store)
        {
            for (const AssetKey key : scene.MaterialKeys())
            {
                if (!store.IsValid(HandleOf<Material>(key)))
                {
                    scene.ReleaseMaterial(key);
                }
            }
            for (const AssetKey key : scene.MeshKeys())
            {
                if (!store.IsValid(HandleOf<Mesh>(key)))
                {
                    scene.ReleaseMesh(key);
                }
            }
            for (const AssetKey key : scene.ShaderKeys())
            {
                if (!store.IsValid(HandleOf<Shader>(key)))
                {
                    scene.ReleaseShader(key);
                }
            }
        }

        /** @brief The active scene's light: the first DirectionalLight, warned once if there are more, and ambient. */
        Render::SceneLighting ReadLighting(Detail::RendererState& state, Scene& scene)
        {
            Render::SceneLighting lighting;
            const AmbientLight ambient =
                scene.GetResource<AmbientLight>() != nullptr ? *scene.GetResource<AmbientLight>() : AmbientLight{};
            lighting.ambient = Math::Vec3(ambient.colour.r, ambient.colour.g, ambient.colour.b) * ambient.intensity;

            EntityList& entities = scene.GetEntityList();
            const std::array<ComponentType, 1> lights{ComponentType::Native<DirectionalLight>()};
            std::uint32_t found = 0;
            entities.ForEachWith(lights, [&](std::uint32_t id) {
                if (found++ > 0)
                {
                    return;
                }
                const Entity entity = entities.GetEntity(id);
                const DirectionalLight& light = entity.GetComponent<DirectionalLight>();
                // It shines along its entity's world -Z.
                const Math::Mat4 world = GetWorldMatrix(entity);
                lighting.lightDirection = glm::normalize(-Math::Vec3(world[2]));
                lighting.lightColour = Math::Vec3(light.colour.r, light.colour.g, light.colour.b) * light.intensity;
            });
            if (found > 1 && !state.loggedLights)
            {
                state.loggedLights = true;
                state.app->Logger()->Warn("FenrirRenderer: the active scene has {} DirectionalLights; only the first "
                                          "is used",
                                          found);
            }
            return lighting;
        }

        /** @brief Every enabled camera in the active scene, each a view of the whole surface. */
        std::vector<Render::ViewRequest> ReadViews(Scene& scene, Graphics::PixelSize size)
        {
            std::vector<Render::ViewRequest> views;
            EntityList& entities = scene.GetEntityList();
            const std::array<ComponentType, 1> cameras{ComponentType::Native<Camera>()};
            const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);
            entities.ForEachWith(cameras, [&](std::uint32_t id) {
                const Entity entity = entities.GetEntity(id);
                const Camera& camera = entity.GetComponent<Camera>();
                if (!camera.enabled)
                {
                    return;
                }
                const Math::Mat4 world = GetWorldMatrix(entity);
                Render::ViewRequest view;
                view.view = glm::inverse(world);
                view.projection =
                    Render::ReversedZPerspective(camera.projection.verticalFov, aspect, camera.projection.near);
                view.position = Math::Vec3(world[3]);
                view.background = BackgroundColour;
                views.push_back(view);
            });
            return views;
        }

        /** @brief The active scene's visible MeshRenderers as draws, preparing what they name. */
        std::vector<Render::DrawRequest> ReadDraws(Detail::RendererState& state, const AssetStore& store, Scene& scene)
        {
            std::vector<Render::DrawRequest> draws;
            EntityList& entities = scene.GetEntityList();
            const std::array<ComponentType, 1> renderers{ComponentType::Native<MeshRenderer>()};
            entities.ForEachWith(renderers, [&](std::uint32_t id) {
                const Entity entity = entities.GetEntity(id);
                const MeshRenderer& renderer = entity.GetComponent<MeshRenderer>();
                if (!renderer.visible || renderer.mesh.IsEmpty())
                {
                    return;
                }
                if (!PrepareMesh(*state.scene, store, renderer.mesh))
                {
                    if (state.staleMeshes.ShouldLog(renderer.mesh))
                    {
                        state.app->Logger()->Warn("FenrirRenderer: entity {} names a stale mesh ({}), so it is not "
                                                  "drawn",
                                                  id, renderer.mesh.index);
                    }
                    return;
                }
                Render::DrawRequest draw;
                draw.mesh = KeyOf(renderer.mesh);
                if (PrepareMaterial(*state.scene, store, renderer.material))
                {
                    draw.material = KeyOf(renderer.material);
                }
                else if (state.fallbackMaterials.ShouldLog(renderer.material))
                {
                    state.app->Logger()->Warn("FenrirRenderer: entity {} names {} material, so it is drawn with the "
                                              "fallback material",
                                              id, renderer.material.IsEmpty() ? "no" : "a stale");
                }
                draw.world = GetWorldMatrix(entity);
                draws.push_back(draw);
            });
            return draws;
        }

        /** @brief One Render: the steps of ADR-0019, all on the main thread. */
        void RenderActiveScene(App& app, Detail::RendererState& state)
        {
            state.time += app.GetTime().deltaTime;
            const AssetStore* store = app.TryGetResource<AssetStore>();
            // Read the window size first: a minimised window builds nothing, but the frame index still advances.
            const Graphics::PixelSize size = state.surface->GetPixelSize();
            Render::SceneFrame frame;
            if (!size.IsEmpty() && store != nullptr)
            {
                Scene& scene = app.GetActiveScene();
                ReleaseStale(*state.scene, *store);
                frame.lighting = ReadLighting(state, scene);
                frame.views = ReadViews(scene, size);
                frame.draws = ReadDraws(state, *store, scene);
                frame.time = static_cast<float>(state.time);
                frame.deltaTime = static_cast<float>(app.GetTime().deltaTime);
                if (frame.views.empty() && !state.loggedNoCamera)
                {
                    state.loggedNoCamera = true;
                    app.Logger()->Warn("FenrirRenderer: the active scene has no enabled Camera, so the main window is "
                                       "cleared to black");
                }
            }
            state.scene->SetFrame(std::move(frame));

            const Render::FrameReport report = state.core->RenderFrame();
            for (const std::string& error : report.errors)
            {
                app.Logger()->Error("FenrirRenderer: {}", error);
            }
            if (report.drawn)
            {
                const Render::SceneStats scene = state.scene->TakeStats();
                state.stats.draws = scene.draws;
                state.stats.passes = report.passesRun;
                state.stats.uploads = scene.uploads;
            }
        }

        /** @brief Registers every built-in shader under its `builtin:` name; false, logged, if one is refused. */
        bool RegisterBuiltins(AssetStore& store)
        {
            for (const Render::BuiltinShader& builtin : Render::BuiltinShaders())
            {
                ShaderData data;
                data.bytes.assign(builtin.bytes.begin(), builtin.bytes.end());
                if (store.AddBuiltin<Shader>(builtin.name, std::move(data)).IsEmpty())
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    namespace Detail
    {
        Graphics::PixelSize WindowSurface::GetPixelSize() const
        {
            if (Window::IsMinimised(*m_app, Window::MainWindow()))
            {
                return {};
            }
            const WindowSize size = Window::PixelSize(*m_app, Window::MainWindow());
            if (size.width <= 0 || size.height <= 0)
            {
                return {};
            }
            return {static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height)};
        }

        void* WindowSurface::GetProcAddress(const char* name)
        {
            return Window::GetProcAddress(*m_app, Window::MainWindow(), name);
        }

        bool WindowSurface::MakeCurrent()
        {
            return Window::MakeCurrent(*m_app, Window::MainWindow());
        }

        void WindowSurface::Present()
        {
            Window::Present(*m_app, Window::MainWindow());
        }

        bool WindowSurface::SetSwapInterval(int interval)
        {
            return Window::SetSwapInterval(*m_app, Window::MainWindow(), interval);
        }

        Graphics::PixelSize NullSurface::GetPixelSize() const
        {
            if (sizeOverride.has_value())
            {
                return *sizeOverride;
            }
            if (!Window::Exists(*m_app, Window::MainWindow()))
            {
                return FixedSize;
            }
            if (Window::IsMinimised(*m_app, Window::MainWindow()))
            {
                return {};
            }
            const WindowSize size = Window::PixelSize(*m_app, Window::MainWindow());
            if (size.width <= 0 || size.height <= 0)
            {
                return {};
            }
            return {static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height)};
        }

        RendererState::~RendererState()
        {
            // The core waits for the GPU and frees every resource while the context is still current; then the
            // surface goes, before the Window tears down (ADR-0022).
            core.reset();
            if (holdsSurface && app != nullptr)
            {
                Window::ReleaseSurface(*app, Window::MainWindow());
            }
        }
    } // namespace Detail

    Renderer Renderer::Install(App& app, RendererConfig config)
    {
        AssetStore* store = app.TryGetResource<AssetStore>();
        if (store == nullptr)
        {
            app.Logger()->Error("FenrirRenderer: the app has no AssetStore; install FenrirAssets before the Renderer");
            return Renderer(nullptr);
        }

        auto state = std::make_shared<Detail::RendererState>();
        state->app = &app;
        state->config = config;
        Graphics::DeviceConfig device;
        device.framesInFlight = config.framesInFlight == 0 ? 1 : config.framesInFlight;
        device.debugValidation = config.debugValidation;

        std::unique_ptr<Graphics::GraphicsDevice> made;
        if (config.backend == GraphicsBackend::Null)
        {
            state->surface = std::make_unique<Detail::NullSurface>(app);
            made = Graphics::CreateNullDevice(device);
        }
        else
        {
            SurfaceRequest request;
            request.api = GraphicsApi::OpenGL;
            request.debugContext = config.debugValidation;
            std::string error;
            if (!Window::RequestSurface(app, Window::MainWindow(), request, error))
            {
                app.Logger()->Error("FenrirRenderer: the OpenGL backend cannot start without the main window's "
                                    "graphics surface: {}",
                                    error);
                return Renderer(nullptr);
            }
            state->holdsSurface = true;
            state->surface = std::make_unique<Detail::WindowSurface>(app);
            Graphics::OpenGLDeviceResult result = Graphics::CreateOpenGLDevice(*state->surface, device, *app.Logger());
            if (result.device == nullptr)
            {
                app.Logger()->Error("FenrirRenderer: the OpenGL backend cannot start: {}", result.diagnostic);
                // The state's teardown releases the surface.
                return Renderer(nullptr);
            }
            made = std::move(result.device);
        }

        if (!RegisterBuiltins(*store))
        {
            app.Logger()->Error("FenrirRenderer: the asset store refused a built-in shader");
            return Renderer(nullptr);
        }
        state->core = std::make_unique<Render::RendererCore>(std::move(made), *state->surface);
        auto scene = std::make_unique<Render::SceneRenderer>(state->core->Device(), *app.Logger());
        state->scene = scene.get();
        state->core->AddFeature(std::move(scene));

        const Graphics::Vsync wanted = ToDevice(config.vsync);
        state->core->Device().SetVsync(wanted);

        // Sequential, so it runs on the main thread, where the GL context is current (ADR-0020).
        app.AddSequentialSystem(SchedulePriority::Render,
                                [installed = std::weak_ptr<Detail::RendererState>(state)](App& running) {
                                    if (const std::shared_ptr<Detail::RendererState> current = installed.lock())
                                    {
                                        RenderActiveScene(running, *current);
                                    }
                                });
        return Renderer(std::move(state));
    }

    Renderer::Renderer(std::shared_ptr<Detail::RendererState> state) : m_state(std::move(state))
    {
    }

    Renderer::Renderer(Renderer&& other) noexcept = default;
    Renderer& Renderer::operator=(Renderer&& other) noexcept = default;
    Renderer::~Renderer() = default;

    Vsync Renderer::SetVsync(Vsync mode)
    {
        if (m_state == nullptr)
        {
            return Vsync::Off;
        }
        return FromDevice(m_state->core->Device().SetVsync(ToDevice(mode)));
    }

    std::optional<Math::AABB> Renderer::GetWorldBounds(const Entity& entity) const
    {
        if (m_state == nullptr || !entity.IsValid() || !entity.HasComponent<MeshRenderer>())
        {
            return std::nullopt;
        }
        const AssetStore* store = m_state->app->TryGetResource<AssetStore>();
        const MeshData* mesh = store == nullptr ? nullptr : store->TryGet(entity.GetComponent<MeshRenderer>().mesh);
        if (mesh == nullptr)
        {
            return std::nullopt;
        }
        // The box around the local box's eight corners, each moved to world space.
        const Math::Mat4 world = GetWorldMatrix(entity);
        Math::AABB bounds;
        for (int corner = 0; corner < 8; ++corner)
        {
            const Math::Vec3 local((corner & 1) != 0 ? mesh->bounds.max.x : mesh->bounds.min.x,
                                   (corner & 2) != 0 ? mesh->bounds.max.y : mesh->bounds.min.y,
                                   (corner & 4) != 0 ? mesh->bounds.max.z : mesh->bounds.min.z);
            const Math::Vec3 point(world * Math::Vec4(local, 1.0f));
            bounds.min = corner == 0 ? point : glm::min(bounds.min, point);
            bounds.max = corner == 0 ? point : glm::max(bounds.max, point);
        }
        return bounds;
    }

    RendererStats Renderer::Stats() const
    {
        return m_state == nullptr ? RendererStats{} : m_state->stats;
    }
} // namespace Fenrir
