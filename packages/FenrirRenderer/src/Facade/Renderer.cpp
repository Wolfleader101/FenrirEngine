#include "FenrirRenderer/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <utility>

#include "Backend/OpenGL/OpenGLDevice.hpp"
#include "Core/BuiltinShaders.hpp"
#include "Facade/RendererState.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/ImageData.hpp"
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

        // A WindowTarget's default is the main window.
        static_assert(WindowTarget{}.window == Window::MainWindow());

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

        /** @brief Whether `image` is a render target: a flat RGBA8 sRGB image with a size and no pixels (ADR-0026). */
        bool IsRenderTarget(const ImageData& image)
        {
            return image.pixels.empty() && image.format == ImageFormat::RGBA8 &&
                   image.colourSpace == ColourSpace::Srgb && image.shape == ImageShape::Flat && image.width > 0 &&
                   image.height > 0;
        }

        /**
         * @brief Whether the image is a render target whose texture is ready: prepared now if it is new or resized. An
         * image that is not (or no longer) a render target has its texture released.
         */
        bool PrepareTarget(Render::SceneRenderer& scene, const AssetStore& store, AssetHandle<Image> handle)
        {
            const ImageData* data = store.TryGet(handle);
            if (data == nullptr || !IsRenderTarget(*data))
            {
                scene.ReleaseTarget(KeyOf(handle));
                return false;
            }
            const std::uint32_t revision = store.Revision(handle);
            if (!scene.IsTargetCurrent(KeyOf(handle), revision))
            {
                scene.PrepareTarget(KeyOf(handle), revision, {data->width, data->height});
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
            // Its shader first: the material's block is laid out from the shader's reflection. Then the render target
            // images it samples, whose textures it binds.
            PrepareShader(scene, store, data->shader);
            for (const auto& [name, value] : data->overrides)
            {
                const auto* texture = std::get_if<MaterialTexture>(&value);
                if (texture != nullptr && !texture->image.IsEmpty())
                {
                    PrepareTarget(scene, store, texture->image);
                }
            }
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
            for (const AssetKey key : scene.TargetKeys())
            {
                if (!store.IsValid(HandleOf<Image>(key)))
                {
                    scene.ReleaseTarget(key);
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

        /** @brief Why a camera draws no view. */
        enum class CameraProblem : std::uint8_t
        {
            None,
            OtherWindow,
            NoImage,
            StaleImage,
            NotRenderTarget,
            TinyViewport,
        };

        /** @brief A camera resolved against its render target as it is now. */
        struct ResolvedCamera
        {
            CameraProblem problem = CameraProblem::None;
            /** @brief The camera's world matrix. */
            Math::Mat4 world{1.0f};
            /** @brief View to clip, reversed-Z. */
            Math::Mat4 projection{1.0f};
            /** @brief The image it draws into; empty for the main window. */
            AssetHandle<Image> image;
            /** @brief Its viewport in its target's pixels. */
            Graphics::PixelRect pixels;
            /**
             * @brief The target's size in the units picking takes points in: logical units for a window, pixels for an
             * image.
             */
            Math::Vec2 pickingSize{0.0f};
            /** @brief The target's size in pixels. */
            Graphics::PixelSize pixelSize;
        };

        /** @brief The main window's size in logical units; with no window, the null surface's size in pixels. */
        Math::Vec2 MainWindowLogicalSize(Detail::RendererState& state)
        {
            if (Window::Exists(*state.app, Window::MainWindow()))
            {
                const WindowSize size = Window::LogicalSize(*state.app, Window::MainWindow());
                return {static_cast<float>(size.width), static_cast<float>(size.height)};
            }
            const Graphics::PixelSize size = state.surface->GetPixelSize();
            return {static_cast<float>(size.width), static_cast<float>(size.height)};
        }

        /**
         * @brief Where and how `camera` on `entity` draws now: its projection from its viewport's pixel size, and the
         * viewport in its target's pixels, or why it draws nothing.
         */
        ResolvedCamera ResolveCamera(Detail::RendererState& state, const AssetStore* store, const Entity& entity,
                                     const Camera& camera)
        {
            ResolvedCamera resolved;
            if (camera.target.Is<WindowTarget>())
            {
                if (camera.target.As<WindowTarget>().window != Window::MainWindow())
                {
                    resolved.problem = CameraProblem::OtherWindow;
                    return resolved;
                }
                resolved.pixelSize = state.surface->GetPixelSize();
                resolved.pickingSize = MainWindowLogicalSize(state);
            }
            else
            {
                resolved.image = camera.target.As<ImageTarget>().image;
                const ImageData* data = store == nullptr ? nullptr : store->TryGet(resolved.image);
                if (resolved.image.IsEmpty() || data == nullptr)
                {
                    resolved.problem = resolved.image.IsEmpty() ? CameraProblem::NoImage : CameraProblem::StaleImage;
                    return resolved;
                }
                if (!IsRenderTarget(*data))
                {
                    resolved.problem = CameraProblem::NotRenderTarget;
                    return resolved;
                }
                resolved.pixelSize = {data->width, data->height};
                resolved.pickingSize = {static_cast<float>(data->width), static_cast<float>(data->height)};
            }

            const Viewport& viewport = camera.viewport;
            resolved.pixels =
                Render::ViewportPixels(viewport.x, viewport.y, viewport.width, viewport.height, resolved.pixelSize);
            if (resolved.pixels.width == 0 || resolved.pixels.height == 0)
            {
                resolved.problem = CameraProblem::TinyViewport;
                return resolved;
            }
            const float aspect = static_cast<float>(resolved.pixels.width) / static_cast<float>(resolved.pixels.height);
            if (camera.projection.Is<Perspective>())
            {
                const Perspective& perspective = camera.projection.As<Perspective>();
                resolved.projection = Render::ReversedZPerspective(perspective.verticalFov, aspect, perspective.near);
            }
            else
            {
                const Orthographic& orthographic = camera.projection.As<Orthographic>();
                resolved.projection =
                    Render::ReversedZOrthographic(orthographic.height, aspect, orthographic.near, orthographic.far);
            }
            resolved.world = GetWorldMatrix(entity);
            return resolved;
        }

        /** @brief A camera's viewport in picking units: its pixel rectangle scaled to them, from the top-left. */
        struct PickingRect
        {
            float left = 0.0f;
            float top = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
        };

        /** @brief `camera`'s viewport in the units picking takes points in. */
        PickingRect PickingRectOf(const ResolvedCamera& camera)
        {
            const float scaleX = camera.pickingSize.x / static_cast<float>(camera.pixelSize.width);
            const float scaleY = camera.pickingSize.y / static_cast<float>(camera.pixelSize.height);
            return {static_cast<float>(camera.pixels.x) * scaleX, static_cast<float>(camera.pixels.y) * scaleY,
                    static_cast<float>(camera.pixels.width) * scaleX,
                    static_cast<float>(camera.pixels.height) * scaleY};
        }

        /** @brief Whether `point` is inside `rect`, its right and bottom edges excluded. */
        bool Holds(const PickingRect& rect, Math::Vec2 point)
        {
            return point.x >= rect.left && point.x < rect.left + rect.width && point.y >= rect.top &&
                   point.y < rect.top + rect.height;
        }

        /** @brief Logs once per camera entity and problem why a camera draws nothing. */
        void LogProblem(Detail::RendererState& state, std::uint32_t id, const Camera& camera, CameraProblem problem)
        {
            if (!state.loggedCameras.insert({id, static_cast<std::uint8_t>(problem)}).second)
            {
                return;
            }
            ILogger& log = *state.app->Logger();
            if (problem == CameraProblem::OtherWindow)
            {
                log.Warn("FenrirRenderer: camera entity {} targets window {}, not the main window, so it is not drawn",
                         id, camera.target.As<WindowTarget>().window.value);
            }
            else if (problem == CameraProblem::NoImage)
            {
                log.Warn("FenrirRenderer: camera entity {} has no target image, so it is not drawn", id);
            }
            else if (problem == CameraProblem::StaleImage)
            {
                log.Warn("FenrirRenderer: camera entity {} names a stale target image ({}), so it is not drawn", id,
                         camera.target.As<ImageTarget>().image.index);
            }
            else if (problem == CameraProblem::NotRenderTarget)
            {
                log.Warn("FenrirRenderer: camera entity {} targets image {}, which is not a render target (a flat "
                         "RGBA8 sRGB image with no pixels), so it is not drawn",
                         id, camera.target.As<ImageTarget>().image.index);
            }
            else if (problem == CameraProblem::TinyViewport)
            {
                log.Warn("FenrirRenderer: camera entity {} has a viewport under one pixel of its target, so it is not "
                         "drawn",
                         id);
            }
        }

        /**
         * @brief Every enabled camera in the active scene that can draw, as a view, preparing the images they draw
         * into, in the order they draw: by order, then entity id, a tie in one target logged once.
         */
        std::vector<Render::ViewRequest> ReadViews(Detail::RendererState& state, const AssetStore& store, Scene& scene)
        {
            struct Candidate
            {
                Render::ViewRequest request;
                std::int32_t order = 0;
                std::uint32_t id = 0;
            };
            std::vector<Candidate> candidates;
            EntityList& entities = scene.GetEntityList();
            const std::array<ComponentType, 1> cameras{ComponentType::Native<Camera>()};
            entities.ForEachWith(cameras, [&](std::uint32_t id) {
                const Entity entity = entities.GetEntity(id);
                const Camera& camera = entity.GetComponent<Camera>();
                if (!camera.enabled)
                {
                    return;
                }
                const ResolvedCamera resolved = ResolveCamera(state, &store, entity, camera);
                if (resolved.problem != CameraProblem::None)
                {
                    LogProblem(state, id, camera, resolved.problem);
                    return;
                }
                if (!resolved.image.IsEmpty())
                {
                    PrepareTarget(*state.scene, store, resolved.image);
                }
                Candidate candidate;
                candidate.order = camera.order;
                candidate.id = id;
                Render::ViewRequest& view = candidate.request;
                view.view = glm::inverse(resolved.world);
                view.projection = resolved.projection;
                view.position = Math::Vec3(resolved.world[3]);
                view.background = camera.background.Is<ColourBackground>()
                                      ? camera.background.As<ColourBackground>().colour
                                      : BackgroundColour;
                view.target = KeyOf(resolved.image);
                view.viewport = resolved.pixels;
                candidates.push_back(candidate);
            });

            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                return a.order != b.order ? a.order < b.order : a.id < b.id;
            });
            std::vector<Render::ViewRequest> views;
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                for (std::size_t j = 0; j < i; ++j)
                {
                    const Candidate& a = candidates[j];
                    const Candidate& b = candidates[i];
                    if (a.order == b.order && a.request.target == b.request.target &&
                        state.loggedTies.insert({a.id, b.id}).second)
                    {
                        state.app->Logger()->Warn("FenrirRenderer: cameras {} and {} draw into {} with the same order "
                                                  "{}; the lower entity id draws first",
                                                  a.id, b.id,
                                                  a.request.target.IsEmpty()
                                                      ? std::string("the main window")
                                                      : std::format("image {}", a.request.target.index),
                                                  a.order);
                    }
                }
                views.push_back(candidates[i].request);
            }
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
                frame.views = ReadViews(state, *store, scene);
                frame.draws = ReadDraws(state, *store, scene);
                frame.time = static_cast<float>(state.time);
                frame.deltaTime = static_cast<float>(app.GetTime().deltaTime);
                const bool windowDrawn =
                    std::any_of(frame.views.begin(), frame.views.end(),
                                [](const Render::ViewRequest& view) { return view.target.IsEmpty(); });
                if (!windowDrawn && !state.loggedNoCamera)
                {
                    state.loggedNoCamera = true;
                    app.Logger()->Warn("FenrirRenderer: the active scene has no enabled Camera drawing into the main "
                                       "window, so it is cleared to black");
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

        /** @brief The MSAA sample count to draw with: 1, 2, 4 or 8, the largest not above the asked-for one, logged. */
        std::uint32_t SupportedMsaa(App& app, const RendererConfig& config)
        {
            std::uint32_t samples = 1;
            for (const std::uint32_t supported : {2u, 4u, 8u})
            {
                if (supported <= config.msaa)
                {
                    samples = supported;
                }
            }
            if (samples != config.msaa)
            {
                app.Logger()->Warn("FenrirRenderer: RendererConfig::msaa {} is not 1, 2, 4 or 8, so the scene targets "
                                   "have {} samples",
                                   config.msaa, samples);
            }
            return samples;
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
        auto scene =
            std::make_unique<Render::SceneRenderer>(state->core->Device(), *app.Logger(), SupportedMsaa(app, config));
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

    namespace
    {
        /** @brief `entity`'s camera resolved against its target now; empty if it has none or it draws nothing. */
        std::optional<ResolvedCamera> PickingCamera(Detail::RendererState& state, const Entity& entity)
        {
            if (!entity.IsValid() || !entity.HasComponent<Camera>())
            {
                return std::nullopt;
            }
            ResolvedCamera resolved =
                ResolveCamera(state, state.app->TryGetResource<AssetStore>(), entity, entity.GetComponent<Camera>());
            if (resolved.problem != CameraProblem::None)
            {
                return std::nullopt;
            }
            return resolved;
        }
    } // namespace

    std::optional<Math::Ray> Renderer::ScreenPointToRay(const Entity& camera, Math::Vec2 point) const
    {
        if (m_state == nullptr)
        {
            return std::nullopt;
        }
        const std::optional<ResolvedCamera> resolved = PickingCamera(*m_state, camera);
        if (!resolved)
        {
            return std::nullopt;
        }
        const PickingRect rect = PickingRectOf(*resolved);
        if (!Holds(rect, point))
        {
            return std::nullopt;
        }
        // The point in normalised device coordinates, +y up.
        const float x = 2.0f * (point.x - rect.left) / rect.width - 1.0f;
        const float y = 1.0f - 2.0f * (point.y - rect.top) / rect.height;
        const float aspect = static_cast<float>(resolved->pixels.width) / static_cast<float>(resolved->pixels.height);

        // In the camera's local space, looking along -Z: a perspective ray leaves the camera's position through the
        // point, and an orthographic one leaves the point in the camera's plane along -Z.
        Math::Vec3 origin(0.0f);
        Math::Vec3 direction(0.0f, 0.0f, -1.0f);
        const Projection& projection = camera.GetComponent<Camera>().projection;
        if (projection.Is<Perspective>())
        {
            const float tanHalf = std::tan(projection.As<Perspective>().verticalFov * 0.5f);
            direction = Math::Vec3(x * tanHalf * aspect, y * tanHalf, -1.0f);
        }
        else
        {
            const float halfHeight = projection.As<Orthographic>().height * 0.5f;
            origin = Math::Vec3(x * halfHeight * aspect, y * halfHeight, 0.0f);
        }
        const Math::Mat4& world = resolved->world;
        return Math::Ray(Math::Vec3(world * Math::Vec4(origin, 1.0f)), Math::Vec3(world * Math::Vec4(direction, 0.0f)));
    }

    std::optional<Math::Vec2> Renderer::WorldToScreenPoint(const Entity& camera, Math::Vec3 point) const
    {
        if (m_state == nullptr)
        {
            return std::nullopt;
        }
        const std::optional<ResolvedCamera> resolved = PickingCamera(*m_state, camera);
        if (!resolved)
        {
            return std::nullopt;
        }
        const Math::Vec4 local = glm::inverse(resolved->world) * Math::Vec4(point, 1.0f);
        // In front of the camera is local -Z.
        if (local.z >= 0.0f)
        {
            return std::nullopt;
        }
        const Math::Vec4 clip = resolved->projection * local;
        const float x = clip.x / clip.w;
        const float y = clip.y / clip.w;
        const PickingRect rect = PickingRectOf(*resolved);
        return Math::Vec2(rect.left + (x + 1.0f) * 0.5f * rect.width, rect.top + (1.0f - y) * 0.5f * rect.height);
    }

    Entity Renderer::CameraAt(WindowId window, Math::Vec2 point) const
    {
        if (m_state == nullptr || window != Window::MainWindow())
        {
            return {};
        }
        Scene& scene = m_state->app->GetActiveScene();
        EntityList& entities = scene.GetEntityList();
        const std::array<ComponentType, 1> cameras{ComponentType::Native<Camera>()};
        Entity found;
        std::int32_t foundOrder = 0;
        // The view drawn last at the point: the highest order, and of equal orders the highest entity id.
        entities.ForEachWith(cameras, [&](std::uint32_t id) {
            const Entity entity = entities.GetEntity(id);
            const Camera& camera = entity.GetComponent<Camera>();
            if (!camera.enabled || !camera.target.Is<WindowTarget>())
            {
                return;
            }
            const std::optional<ResolvedCamera> resolved = PickingCamera(*m_state, entity);
            if (!resolved || !Holds(PickingRectOf(*resolved), point))
            {
                return;
            }
            if (!found.IsValid() || camera.order > foundOrder || (camera.order == foundOrder && id > found.GetId()))
            {
                found = entity;
                foundOrder = camera.order;
            }
        });
        return found;
    }

} // namespace Fenrir
