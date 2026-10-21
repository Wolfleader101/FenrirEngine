// SPIKE (prototype/fenrir-renderer-spike, throwaway): the ECS facade (ADR-0019, ADR-0022, ADR-0023). The only code
// that links FenrirWindow; it adapts it to the core's GraphicsSurface.

#include "FenrirRenderer/Renderer.hpp"

#include "Device/Device.hpp"
#include "RendererCore.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirWindow/Window.hpp"

#include <algorithm>
#include <utility>

namespace Fenrir
{
    namespace
    {
        class WindowSurface final : public Gpu::GraphicsSurface
        {
          public:
            explicit WindowSurface(App& app) : m_app(app)
            {
            }
            void* GetProcAddress(const char* name) override
            {
                return Window::GetProcAddress(m_app, name);
            }
            bool MakeCurrent() override
            {
                return Window::MakeCurrent(m_app, Window::MainWindow());
            }
            void Present() override
            {
                Window::Present(m_app, Window::MainWindow());
            }
            bool SetSwapInterval(int interval) override
            {
                return Window::SetSwapInterval(m_app, interval);
            }
            void GetPixelSize(int& width, int& height) override
            {
                const PixelSize size = Window::GetPixelSize(m_app, Window::MainWindow());
                width = size.width;
                height = size.height;
            }

          private:
            App& m_app;
        };

        template <typename T>
        Render::AssetKey KeyOf(AssetHandle<T> handle)
        {
            return Render::RendererCore::Key(handle.index, handle.generation);
        }

        int SwapInterval(VSync vsync)
        {
            return vsync == VSync::Off ? 0 : vsync == VSync::On ? 1 : -1;
        }
    } // namespace

    namespace Detail
    {
        struct RendererState
        {
            RendererConfig config;
            std::unique_ptr<WindowSurface> surface;
            std::unique_ptr<Render::RendererCore> core;
            RendererStats stats;
            bool warnedLights = false;
        };
    } // namespace Detail

    namespace
    {
        Detail::RendererState* State(App& app)
        {
            return app.GetResource<Detail::RendererState>();
        }

        void PrepareImage(Render::RendererCore& core, const Assets& assets, AssetHandle<ImageData> handle)
        {
            const ImageData* image = assets.Get(handle);
            const std::uint32_t revision = assets.Revision(handle);
            if (image != nullptr && !core.IsImageCurrent(KeyOf(handle), revision))
                core.PrepareImage(KeyOf(handle), revision, *image);
        }

        void RenderSystem(App& app)
        {
            Detail::RendererState* state = State(app);
            const Assets* assets = app.GetResource<Assets>();
            if (state == nullptr || state->core == nullptr || assets == nullptr)
                return;
            Render::RendererCore& core = *state->core;
            if (state->surface != nullptr && Window::IsMinimised(app, Window::MainWindow()))
            {
                core.SkipFrame(); // nothing built, submitted or presented; the frame index still moves (ADR-0022)
                return;
            }

            Scene& scene = app.GetActiveScene();
            EntityList& entities = scene.GetEntityList();
            Render::FrameInput input;
            input.time = static_cast<float>(app.GetTime().deltaTime);

            // The prepare step: whatever is new or changed, as (key, revision, data) (ADR-0023).
            const Sky* sky = scene.GetResource<Sky>();
            if (sky != nullptr)
            {
                input.skyColour = sky->colour;
                if (sky->cubemap.IsValid())
                {
                    PrepareImage(core, *assets, sky->cubemap);
                    input.skyCubemap = KeyOf(sky->cubemap);
                }
            }
            if (const AmbientLight* ambient = scene.GetResource<AmbientLight>())
            {
                input.ambient = ambient->colour;
                input.ambientIntensity = ambient->intensity;
            }

            int lights = 0;
            entities.ForEach<DirectionalLight>([&](auto id, DirectionalLight& light) {
                if (lights++ > 0)
                    return;
                const Math::Mat4 world = GetWorldMatrix(entities.GetEntity(static_cast<std::uint32_t>(id)));
                input.hasLight = true;
                input.lightDirection = -Math::Vec3(world[2]);
                input.lightColour = light.colour;
                input.lightIntensity = light.intensity;
            });
            if (lights > 1 && !state->warnedLights)
            {
                state->warnedLights = true;
                app.Logger()->Warn("FenrirRenderer: more than one DirectionalLight; only the first is used");
            }

            entities.ForEach<MeshRenderer>([&](auto id, MeshRenderer& renderer) {
                if (!renderer.visible)
                    return;
                if (const MaterialData* material = assets->Get(renderer.material))
                {
                    for (const auto& [name, value] : material->params)
                        if (const auto* image = std::get_if<AssetHandle<ImageData>>(&value))
                            PrepareImage(core, *assets, *image);
                    const std::uint32_t revision = assets->Revision(renderer.material);
                    if (!core.IsMaterialCurrent(KeyOf(renderer.material), revision))
                        core.PrepareMaterial(KeyOf(renderer.material), revision, *material);
                }
                if (const MeshData* mesh = assets->Get(renderer.mesh))
                {
                    const std::uint32_t revision = assets->Revision(renderer.mesh);
                    if (!core.IsMeshCurrent(KeyOf(renderer.mesh), revision))
                        core.PrepareMesh(KeyOf(renderer.mesh), revision, *mesh);
                }
                Render::DrawItemInput item;
                item.mesh = assets->Get(renderer.mesh) ? KeyOf(renderer.mesh) : 0;
                item.material = assets->Get(renderer.material) ? KeyOf(renderer.material) : 0;
                item.world = GetWorldMatrix(entities.GetEntity(static_cast<std::uint32_t>(id)));
                input.items.push_back(item);
            });

            // Every enabled camera draws a view, in `order` (ADR-0026).
            int width = 0, height = 0;
            if (state->surface != nullptr)
                state->surface->GetPixelSize(width, height);
            else
                width = 640, height = 360;
            std::vector<std::pair<int, Render::ViewInput>> views;
            entities.ForEach<Camera>([&](auto id, Camera& camera) {
                if (!camera.enabled)
                    return;
                const Math::Mat4 world = GetWorldMatrix(entities.GetEntity(static_cast<std::uint32_t>(id)));
                Render::ViewInput view;
                view.view = glm::inverse(world);
                view.position = Math::Vec3(world[3]);
                const float aspect = (camera.viewport[2] * static_cast<float>(width)) /
                                     std::max(1.0f, camera.viewport[3] * static_cast<float>(height));
                view.projection = Render::ReversedInfinitePerspective(camera.fovY, aspect, camera.near);
                std::copy(std::begin(camera.viewport), std::end(camera.viewport), view.viewport);
                view.drawDebug = camera.drawDebug;
                view.hasClearColour = camera.overrideClear;
                view.clearColour = camera.clearColour;
                views.emplace_back(camera.order, view);
            });
            std::stable_sort(views.begin(), views.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& [order, view] : views)
                input.views.push_back(view);

            if (const DebugDraw* debug = scene.GetResource<DebugDraw>())
                input.lines = debug->Lines();

            Render::FrameStats stats = core.RenderFrame(input);
            state->stats.items = stats.items;
            state->stats.draws = stats.draws;
            state->stats.passes = stats.passes;
            state->stats.culledPasses = stats.culledPasses;
            state->stats.uploads = stats.uploads;
            state->stats.pipelines = stats.pipelines;
            state->stats.liveBuffers = stats.liveBuffers;
            state->stats.liveTextures = stats.liveTextures;
            state->stats.passOrder = std::move(stats.passOrder);
        }
    } // namespace

    Renderer Renderer::Install(App& app, RendererConfig config)
    {
        Detail::RendererState& state = app.AddResource<Detail::RendererState>();
        state.config = config;
        std::string error;
        std::unique_ptr<Gpu::GraphicsDevice> device;
        if (config.backend == GraphicsBackend::Null)
        {
            device = Gpu::CreateNullDevice();
        }
        else
        {
            if (!Window::IsInstalled(app))
            {
                app.Logger()->Error("FenrirRenderer: the OpenGL backend needs FenrirWindow installed first");
                app.RemoveResource<Detail::RendererState>();
                return Renderer(nullptr);
            }
            SurfaceRequest request;
            request.debugContext = config.debugValidation;
            if (!Window::RequestSurface(app, Window::MainWindow(), request, error))
            {
                app.Logger()->Error("FenrirRenderer: no OpenGL 4.6 core context: " + error);
                app.RemoveResource<Detail::RendererState>();
                return Renderer(nullptr);
            }
            state.surface = std::make_unique<WindowSurface>(app);
            device = Gpu::CreateOpenGLDevice(*state.surface, config.debugValidation, error);
            if (device == nullptr)
            {
                app.Logger()->Error("FenrirRenderer: " + error);
                Window::ReleaseSurface(app, Window::MainWindow());
                app.RemoveResource<Detail::RendererState>();
                return Renderer(nullptr);
            }
        }
        state.core = std::make_unique<Render::RendererCore>(std::move(device), [&app](const std::string& message) {
            app.Logger()->Warn("FenrirRenderer: " + message);
        });
        if (!state.core->Init(error))
        {
            app.Logger()->Error("FenrirRenderer: " + error);
            state.core.reset();
            if (state.surface)
                Window::ReleaseSurface(app, Window::MainWindow());
            app.RemoveResource<Detail::RendererState>();
            return Renderer(nullptr);
        }
        if (state.surface != nullptr && !state.core->SetSwapInterval(SwapInterval(config.vsync)))
        {
            app.Logger()->Warn("FenrirRenderer: vsync mode unsupported, falling back to On");
            state.core->SetSwapInterval(1);
        }
        // On the main thread, where the context is current: AddSystem would run it on a worker.
        app.AddSequentialSystems(SchedulePriority::Render, {RenderSystem});
        return Renderer(&app);
    }

    Renderer::Renderer(App* app) : m_app(app)
    {
    }

    Renderer::Renderer(Renderer&& other) noexcept : m_app(std::exchange(other.m_app, nullptr))
    {
    }

    Renderer& Renderer::operator=(Renderer&& other) noexcept
    {
        std::swap(m_app, other.m_app);
        return *this;
    }

    Renderer::~Renderer()
    {
        if (m_app == nullptr)
            return;
        if (Detail::RendererState* state = State(*m_app))
        {
            const bool hadSurface = state->surface != nullptr;
            state->core.reset(); // frees device resources while the context is still current
            if (hadSurface)
                Window::ReleaseSurface(*m_app, Window::MainWindow());
            m_app->RemoveResource<Detail::RendererState>();
        }
    }

    bool Renderer::IsValid() const
    {
        return m_app != nullptr;
    }

    void Renderer::SetVSync(VSync vsync)
    {
        Detail::RendererState* state = m_app ? State(*m_app) : nullptr;
        if (state != nullptr && state->core != nullptr && !state->core->SetSwapInterval(SwapInterval(vsync)))
        {
            m_app->Logger()->Warn("FenrirRenderer: vsync mode unsupported, falling back to On");
            state->core->SetSwapInterval(1);
        }
    }

    RendererStats Renderer::Stats() const
    {
        Detail::RendererState* state = m_app ? State(*m_app) : nullptr;
        return state ? state->stats : RendererStats{};
    }

    bool Renderer::GetWorldBounds(const Entity& entity, Math::Vec3& min, Math::Vec3& max) const
    {
        const Assets* assets = m_app ? m_app->GetResource<Assets>() : nullptr;
        if (assets == nullptr || !entity.HasComponent<MeshRenderer>())
            return false;
        const MeshData* mesh = assets->Get(entity.GetComponent<MeshRenderer>().mesh);
        if (mesh == nullptr)
            return false;
        const Math::Mat4 world = GetWorldMatrix(entity);
        for (int i = 0; i < 8; ++i)
        {
            const Math::Vec3 corner((i & 1) ? mesh->boundsMax.x : mesh->boundsMin.x,
                                    (i & 2) ? mesh->boundsMax.y : mesh->boundsMin.y,
                                    (i & 4) ? mesh->boundsMax.z : mesh->boundsMin.z);
            const Math::Vec3 p(world * Math::Vec4(corner, 1.0f));
            min = i == 0 ? p : glm::min(min, p);
            max = i == 0 ? p : glm::max(max, p);
        }
        return true;
    }

    std::vector<std::uint8_t> Renderer::ReadBackbuffer(int& width, int& height)
    {
        Detail::RendererState* state = m_app ? State(*m_app) : nullptr;
        if (state == nullptr || state->core == nullptr)
            return {};
        return state->core->ReadBackbuffer(width, height);
    }
} // namespace Fenrir
