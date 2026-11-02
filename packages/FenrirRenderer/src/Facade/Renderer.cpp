#include "FenrirRenderer/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "Backend/OpenGL/OpenGLDevice.hpp"
#include "Core/BuiltinShaders.hpp"
#include "Facade/RendererState.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
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

        static_assert(static_cast<int>(Tonemapper::None) == static_cast<int>(Render::Tonemapper::None) &&
                      static_cast<int>(Tonemapper::AgX) == static_cast<int>(Render::Tonemapper::AgX) &&
                      static_cast<int>(Tonemapper::PbrNeutral) == static_cast<int>(Render::Tonemapper::PbrNeutral) &&
                      static_cast<int>(Tonemapper::Aces) == static_cast<int>(Render::Tonemapper::Aces));

        Render::Tonemapper ToCore(Tonemapper tonemapper)
        {
            return static_cast<Render::Tonemapper>(tonemapper);
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
            // An image replaced by a render target has no pixels to upload any more.
            scene.ReleaseImage(KeyOf(handle));
            const std::uint32_t revision = store.Revision(handle);
            if (!scene.IsTargetCurrent(KeyOf(handle), revision))
            {
                scene.PrepareTarget(KeyOf(handle), revision, {data->width, data->height});
            }
            return true;
        }

        /**
         * @brief Prepares an image a material samples: a render target as one, a flat image with pixels uploaded with
         * its mips. Anything else (a cubemap, or an image with no pixels that is not a render target) is released and
         * logged once, and the material binds its parameter's default texture there.
         */
        void PrepareSampledImage(Detail::RendererState& state, const AssetStore& store, AssetHandle<Image> handle,
                                 std::string_view sampler = "a material")
        {
            Render::SceneRenderer& scene = *state.scene;
            const ImageData* data = store.TryGet(handle);
            if (data != nullptr && IsRenderTarget(*data))
            {
                PrepareTarget(scene, store, handle);
                return;
            }
            scene.ReleaseTarget(KeyOf(handle));
            if (data == nullptr || data->shape != ImageShape::Flat || data->pixels.empty())
            {
                scene.ReleaseImage(KeyOf(handle));
                if (data != nullptr && state.loggedImages.insert({handle.index, handle.generation}).second)
                {
                    state.app->Logger()->Warn("FenrirRenderer: {} samples image {}, which is {}, so it gets its "
                                              "parameter's default texture there",
                                              sampler, handle.index,
                                              data->shape != ImageShape::Flat
                                                  ? "a cubemap"
                                                  : "neither a render target nor an image with pixels");
                }
                return;
            }
            const std::uint32_t revision = store.Revision(handle);
            if (!scene.IsImageCurrent(KeyOf(handle), revision))
            {
                scene.PrepareImage(KeyOf(handle), revision, *data);
            }
        }

        bool PrepareMaterial(Detail::RendererState& state, const AssetStore& store, AssetHandle<Material> handle)
        {
            Render::SceneRenderer& scene = *state.scene;
            const MaterialData* data = store.TryGet(handle);
            if (data == nullptr)
            {
                return false;
            }
            // Its shader first: the material's block is laid out from the shader's reflection. Then the images and
            // render target images it samples, whose textures it binds.
            PrepareShader(scene, store, data->shader);
            for (const auto& [name, value] : data->overrides)
            {
                const auto* texture = std::get_if<MaterialTexture>(&value);
                if (texture != nullptr && !texture->image.IsEmpty())
                {
                    PrepareSampledImage(state, store, texture->image);
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
        void ReleaseStale(Render::SceneRenderer& scene, Render::Draw2DFeature& draw2D, const AssetStore& store)
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
            for (const AssetKey key : draw2D.ShaderKeys())
            {
                if (!store.IsValid(HandleOf<Shader>(key)))
                {
                    draw2D.ReleaseShader(key);
                }
            }
            for (const AssetKey key : scene.TargetKeys())
            {
                if (!store.IsValid(HandleOf<Image>(key)))
                {
                    scene.ReleaseTarget(key);
                }
            }
            for (const AssetKey key : scene.ImageKeys())
            {
                if (!store.IsValid(HandleOf<Image>(key)))
                {
                    scene.ReleaseImage(key);
                }
            }
            for (const AssetKey key : scene.CubemapKeys())
            {
                if (!store.IsValid(HandleOf<Image>(key)))
                {
                    scene.ReleaseCubemap(key);
                }
            }
            for (const AssetKey key : scene.PanoramaKeys())
            {
                if (!store.IsValid(HandleOf<Image>(key)))
                {
                    scene.ReleasePanorama(key);
                }
            }
        }

        /**
         * @brief What a view on the scene's background draws: the Sky's colour, or its prepared cubemap or panorama,
         * with the sky's intensity and rotation.
         */
        struct SceneSky
        {
            Colour colour = ColourSky{}.colour;
            /** @brief The cubemap or panorama image; empty for a colour. */
            AssetKey image;
            float intensity = 1.0f;
            float rotation = 0.0f;
        };

        /** @brief Why a Sky's image cannot be drawn, for its warning; empty if it can. */
        std::string SkyImageProblem(AssetHandle<Image> handle, const ImageData* data, SkyType type)
        {
            const std::string_view kind = type == SkyType::Cubemap ? "cubemap" : "panorama";
            if (handle.IsEmpty())
            {
                return std::format("no {}", kind);
            }
            if (data == nullptr)
            {
                return std::format("a stale {} ({})", kind, handle.index);
            }
            if (type == SkyType::Cubemap && (data->shape != ImageShape::Cubemap || data->pixels.empty()))
            {
                return std::format("image {}, which is not a cubemap with pixels", handle.index);
            }
            if (type == SkyType::Panorama &&
                (data->shape != ImageShape::Flat || data->pixels.empty() || data->width != 2 * data->height))
            {
                return std::format("image {} ({} x {}) as a panorama, which must be a flat image with pixels twice as "
                                   "wide as high",
                                   handle.index, data->width, data->height);
            }
            return {};
        }

        /**
         * @brief `sky`, the active scene's Sky or its SkyLight's (`resource` names which, for the log), its cubemap or
         * panorama prepared if it is new or changed. A cubemap that is empty, stale or not a cubemap with pixels, or a
         * panorama that is empty, stale or not a flat image with pixels twice as wide as high, gives the default
         * ColourSky, logged once per handle. No sky gives the default ColourSky.
         */
        SceneSky ReadSky(Detail::RendererState& state, const AssetStore& store, const Sky* sky,
                         std::string_view resource)
        {
            if (sky == nullptr)
            {
                return {};
            }
            if (sky->Is<ColourSky>())
            {
                return {sky->As<ColourSky>().colour, {}};
            }
            const bool cubemap = sky->Is<CubemapSky>();
            const AssetHandle<Image> handle = cubemap ? sky->As<CubemapSky>().cubemap : sky->As<PanoramaSky>().image;
            const float intensity = cubemap ? sky->As<CubemapSky>().intensity : sky->As<PanoramaSky>().intensity;
            const float rotation = cubemap ? sky->As<CubemapSky>().rotation : sky->As<PanoramaSky>().rotation;
            const ImageData* data = store.TryGet(handle);
            if (const std::string problem = SkyImageProblem(handle, data, sky->Type()); !problem.empty())
            {
                if (state.skyImages.ShouldLog(handle))
                {
                    state.app->Logger()->Warn("FenrirRenderer: the active scene's {} names {}, so it is dark grey",
                                              resource, problem);
                }
                return {};
            }
            const std::uint32_t revision = store.Revision(handle);
            if (cubemap && !state.scene->IsCubemapCurrent(KeyOf(handle), revision))
            {
                state.scene->PrepareCubemap(KeyOf(handle), revision, *data);
            }
            else if (!cubemap && !state.scene->IsPanoramaCurrent(KeyOf(handle), revision))
            {
                state.scene->PreparePanorama(KeyOf(handle), revision, *data);
            }
            return {ColourSky{}.colour, KeyOf(handle), intensity, rotation};
        }

        /**
         * @brief The active scene's light: the first DirectionalLight, warned once if there are more, and `sky`, the
         * sky it is lit by (ADR-0047): its SkyLight's, else its Sky, whatever each camera's background.
         */
        Render::SceneLighting ReadLighting(Detail::RendererState& state, Scene& scene, const SceneSky& sky)
        {
            Render::SceneLighting lighting;
            lighting.sky = sky.image;
            lighting.skyColour = Math::Vec3(sky.colour.r, sky.colour.g, sky.colour.b);
            lighting.skyIntensity = sky.intensity;
            lighting.skyRotation = sky.rotation;

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
        std::vector<Render::ViewRequest> ReadViews(Detail::RendererState& state, const AssetStore& store, Scene& scene,
                                                   const SceneSky& sky)
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
                // A camera's own colour overrides the scene's Sky for its view.
                if (camera.background.Is<ColourBackground>())
                {
                    view.background = camera.background.As<ColourBackground>().colour;
                }
                else
                {
                    view.background = sky.colour;
                    view.sky = sky.image;
                }
                view.target = KeyOf(resolved.image);
                view.viewport = resolved.pixels;
                view.drawDebug = camera.drawDebug;
                view.exposure = camera.exposure;
                view.tonemapper = ToCore(camera.tonemapper);
                view.bloom = {camera.bloom.intensity, camera.bloom.radius};
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

        /** @brief The registered skinning source (ADR-0041), or null with none. */
        SkinningSource* SkinningSourceOf(App& app)
        {
            const SkinningSourceRegistration* registration = app.TryGetResource<SkinningSourceRegistration>();
            return registration == nullptr ? nullptr : registration->source;
        }

        /** @brief The scene of `app` whose entity list holds `entity`, or null. */
        Scene* FindSceneOf(App& app, const Entity& entity)
        {
            Scene* found = nullptr;
            app.ForEachScene([&](Scene& scene) {
                if (found == nullptr && scene.GetEntityList().GetEntity(entity.GetId()) == entity)
                {
                    found = &scene;
                }
            });
            return found;
        }

        /** @brief Why a mesh entity's skinning was skipped or ignored, each logged once per entity. */
        enum class SkinProblem : std::uint8_t
        {
            NoSkinnedMesh,
            NoSkinData,
            DeadSkeleton,
            Missing,
            Unavailable,
            Incompatible,
        };

        /** @brief Logs `problem` for mesh entity `id` the first time it is met. */
        template <typename... Args>
        void LogSkin(Detail::RendererState& state, std::uint32_t id, SkinProblem problem, std::string_view format,
                     Args&&... args)
        {
            if (state.loggedSkins.insert({id, static_cast<std::uint8_t>(problem)}).second)
            {
                state.app->Logger()->Warn(format, std::forward<Args>(args)...);
            }
        }

        /** @brief `box` moved by `matrix`: the box around its eight corners, moved. */
        Math::AABB Moved(const Math::AABB& box, const Math::Mat4& matrix)
        {
            Math::AABB moved;
            for (int corner = 0; corner < 8; ++corner)
            {
                const Math::Vec3 local((corner & 1) != 0 ? box.max.x : box.min.x,
                                       (corner & 2) != 0 ? box.max.y : box.min.y,
                                       (corner & 4) != 0 ? box.max.z : box.min.z);
                const Math::Vec3 point(matrix * Math::Vec4(local, 1.0f));
                moved.min = corner == 0 ? point : glm::min(moved.min, point);
                moved.max = corner == 0 ? point : glm::max(moved.max, point);
            }
            return moved;
        }

        /**
         * @brief A skinned mesh's world bounds (ADR-0035): each bone's box moved by its palette matrix, and the box of
         * unweighted vertices, all under the skeleton entity's `world`. Empty when the skin bounds nothing.
         */
        std::optional<Math::AABB> SkinnedBounds(const MeshSkin& skin, std::span<const Math::Mat4> palette,
                                                const Math::Mat4& world)
        {
            std::optional<Math::AABB> bounds;
            const auto add = [&](const Math::AABB& box) {
                bounds = bounds ? Math::AABB{glm::min(bounds->min, box.min), glm::max(bounds->max, box.max)} : box;
            };
            for (std::size_t bone = 0; bone < skin.boneBounds.size() && bone < palette.size(); ++bone)
            {
                if (skin.boneBounds[bone].has_value())
                {
                    add(Moved(*skin.boneBounds[bone], world * palette[bone]));
                }
            }
            if (skin.unweightedBounds.has_value())
            {
                add(Moved(*skin.unweightedBounds, world));
            }
            return bounds;
        }

        /** @brief Where each skeleton's palette starts in the frame's palette, by scene, skeleton entity and pose. */
        using PaletteRanges = std::map<std::tuple<const Scene*, std::uint32_t, std::uint64_t>, std::uint32_t>;

        /**
         * @brief Makes `draw` skinned from the SkinnedMesh on mesh entity `entity` (ADR-0035, ADR-0041): its skeleton's
         * world matrix and its palette range, the palette appended to `palette` the first time this frame its skeleton
         * is met at this pose. False, logged once, when it may not be drawn.
         */
        bool ReadSkinned(Detail::RendererState& state, const AssetStore& store, Scene& scene, const Entity& entity,
                         std::uint32_t id, AssetHandle<Mesh> mesh, const MeshSkin& skin,
                         std::vector<Math::Mat4>& palette, PaletteRanges& ranges, Render::DrawRequest& draw)
        {
            if (!entity.HasComponent<SkinnedMesh>())
            {
                LogSkin(state, id, SkinProblem::NoSkinnedMesh,
                        "FenrirRenderer: entity {} draws mesh {}, which has skin data, but has no SkinnedMesh naming "
                        "its skeleton, so it is not drawn",
                        id, mesh.index);
                return false;
            }
            SkinningSource* source = SkinningSourceOf(*state.app);
            if (source == nullptr)
            {
                if (!state.loggedNoSkinningSource)
                {
                    state.loggedNoSkinningSource = true;
                    state.app->Logger()->Warn("FenrirRenderer: no skinning source is registered (install "
                                              "FenrirSkeleton), so skinned meshes are not drawn");
                }
                return false;
            }
            const Entity skeleton = entity.GetComponent<SkinnedMesh>().skeleton;
            if (!skeleton.IsValid())
            {
                LogSkin(state, id, SkinProblem::DeadSkeleton,
                        "FenrirRenderer: entity {} is skinned by a skeleton entity that is not alive, so it is not "
                        "drawn",
                        id);
                return false;
            }
            const PoseState pose = source->GetPoseState(scene, skeleton);
            if (pose == PoseState::Missing)
            {
                LogSkin(state, id, SkinProblem::Missing,
                        "FenrirRenderer: entity {} is skinned by entity {}, which has no SkeletonPose, so it is not "
                        "drawn",
                        id, skeleton.GetId());
                return false;
            }
            if (pose != PoseState::Ready)
            {
                LogSkin(state, id, SkinProblem::Unavailable,
                        "FenrirRenderer: entity {} is skinned by entity {}, whose skeleton is Unavailable, so it is "
                        "not drawn",
                        id, skeleton.GetId());
                return false;
            }

            // Compatibility (ADR-0035), checked only when the pairing changes.
            const AssetHandle<Skeleton> skeletonAsset = source->GetSkeleton(scene, skeleton);
            const SkeletonData* skeletonData = store.TryGet(skeletonAsset);
            if (skeletonData == nullptr)
            {
                return false;
            }
            Detail::RendererState::SkinPairing pairing{mesh, store.Revision(mesh), skeletonAsset,
                                                       store.Revision(skeletonAsset)};
            Detail::RendererState::SkinPairing& cached = state.skinPairings[id];
            if (!cached.SameAs(pairing))
            {
                const bool fits = skin.boneCount <= skeletonData->BoneCount();
                const bool signatureMatches =
                    skin.expectedSignature == 0 || skin.expectedSignature == skeletonData->BindSignature();
                pairing.compatible = fits && signatureMatches;
                cached = pairing;
                if (!pairing.compatible)
                {
                    // Logged for each pairing that fails, so a mesh given a second wrong skeleton says so again.
                    state.loggedSkins.erase({id, static_cast<std::uint8_t>(SkinProblem::Incompatible)});
                }
            }
            if (!cached.compatible)
            {
                if (skin.boneCount > skeletonData->BoneCount())
                {
                    LogSkin(state, id, SkinProblem::Incompatible,
                            "FenrirRenderer: entity {}'s mesh {} needs {} bones, but the skeleton of entity {} has {}, "
                            "so it is not drawn",
                            id, mesh.index, skin.boneCount, skeleton.GetId(), skeletonData->BoneCount());
                }
                else
                {
                    LogSkin(state, id, SkinProblem::Incompatible,
                            "FenrirRenderer: entity {}'s mesh {} was baked for another skeleton than entity {}'s (its "
                            "bind signature differs), so it is not drawn",
                            id, mesh.index, skeleton.GetId());
                }
                return false;
            }

            // One palette range per skeleton and pose this frame, shared by every mesh and view drawing it.
            const auto [range, added] =
                ranges.try_emplace({&scene, skeleton.GetId(), source->GetPoseGeneration(scene, skeleton)},
                                   static_cast<std::uint32_t>(palette.size()));
            if (added)
            {
                const std::span<const Math::Mat4> bones = source->GetPalette(scene, skeleton);
                palette.insert(palette.end(), bones.begin(), bones.end());
            }
            draw.skinned = true;
            draw.paletteOffset = range->second;
            draw.world = GetWorldMatrix(skeleton);
            const MaterialData* material =
                store.TryGet(AssetHandle<Material>{draw.material.index, draw.material.generation});
            if (material != nullptr &&
                (material->blendMode == BlendMode::AlphaBlend || material->blendMode == BlendMode::Additive))
            {
                if (const auto bounds = SkinnedBounds(skin, source->GetPalette(scene, skeleton), draw.world))
                {
                    draw.sortPoint = (bounds->min + bounds->max) * 0.5f;
                }
            }
            return true;
        }

        /**
         * @brief The active scene's visible MeshRenderers as `frame`'s draws, preparing what they name, and the
         * palettes their skinned draws read.
         */
        void ReadDraws(Detail::RendererState& state, const AssetStore& store, Scene& scene, Render::SceneFrame& frame)
        {
            std::vector<Render::DrawRequest>& draws = frame.draws;
            std::vector<Math::Mat4>& palette = frame.palette;
            PaletteRanges ranges;
            std::set<std::uint32_t> drawn;
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
                if (PrepareMaterial(state, store, renderer.material))
                {
                    draw.material = KeyOf(renderer.material);
                }
                else if (state.fallbackMaterials.ShouldLog(renderer.material))
                {
                    state.app->Logger()->Warn("FenrirRenderer: entity {} names {} material, so it is drawn with the "
                                              "fallback material",
                                              id, renderer.material.IsEmpty() ? "no" : "a stale");
                }
                const MeshData& data = *store.TryGet(renderer.mesh);
                if (data.skin.has_value())
                {
                    drawn.insert(id);
                    if (!ReadSkinned(state, store, scene, entity, id, renderer.mesh, *data.skin, palette, ranges, draw))
                    {
                        return;
                    }
                }
                else
                {
                    if (entity.HasComponent<SkinnedMesh>())
                    {
                        LogSkin(state, id, SkinProblem::NoSkinData,
                                "FenrirRenderer: entity {} has a SkinnedMesh, but its mesh {} has no skin data, so it "
                                "is drawn rigidly",
                                id, renderer.mesh.index);
                    }
                    draw.world = GetWorldMatrix(entity);
                }
                draws.push_back(draw);
            });
            // The compatibility verdicts of mesh entities no longer drawn skinned go.
            std::erase_if(state.skinPairings, [&](const auto& entry) { return !drawn.contains(entry.first); });
            frame.palettes = static_cast<std::uint32_t>(ranges.size());
        }

        /** @brief The core's copy of a FenrirDebugDraw vertex. */
        Render::DebugShapeVertex ToCore(const DebugVertex& vertex)
        {
            return {vertex.position, vertex.colour};
        }

        Render::DebugDepth ToCore(DepthMode depth)
        {
            return depth == DepthMode::OnTop ? Render::DebugDepth::OnTop : Render::DebugDepth::Tested;
        }

        /**
         * @brief The active scene's debug drawing this frame (ADR-0025), tessellated by its DebugDraw and copied into
         * the core's plain data; empty when the scene has no DebugDraw.
         */
        Render::DebugFrame ReadDebug(Detail::RendererState& state, Scene& scene)
        {
            Render::DebugFrame frame;
            const DebugDraw* draw = scene.GetResource<DebugDraw>();
            if (draw == nullptr)
            {
                return frame;
            }
            const DebugBatches batches = draw->Tessellate();
            for (const DebugLineBatch& batch : batches.lines)
            {
                Render::DebugLines& lines = frame.lines.emplace_back();
                lines.depth = ToCore(batch.depth);
                lines.thickness = batch.thickness;
                std::transform(batch.vertices.begin(), batch.vertices.end(), std::back_inserter(lines.vertices),
                               [](const DebugVertex& v) { return ToCore(v); });
            }
            for (const DebugTriangleBatch& batch : batches.triangles)
            {
                Render::DebugTriangles& triangles = frame.triangles.emplace_back();
                triangles.depth = ToCore(batch.depth);
                std::transform(batch.vertices.begin(), batch.vertices.end(), std::back_inserter(triangles.vertices),
                               [](const DebugVertex& v) { return ToCore(v); });
            }
            for (const DebugTextRun& run : batches.texts)
            {
                const Render::DebugText text{run.text, run.position, run.size, run.colour};
                if (run.space == DebugTextSpace::World)
                {
                    frame.worldTexts.push_back(text);
                }
                else if (run.space == DebugTextSpace::Message)
                {
                    frame.messages.push_back(text);
                }
                else
                {
                    frame.screenTexts.push_back(text);
                }
            }
            frame.contentScale = Window::Exists(*state.app, Window::MainWindow())
                                     ? Window::ContentScale(*state.app, Window::MainWindow())
                                     : 1.0f;
            return frame;
        }

        /**
         * @brief The Overlay's submissions this frame (ADR-0027), taken so it starts the next frame empty, with the
         * shaders and images they name prepared. A submission to a window other than the main window is dropped, logged
         * once per window. With `draw` false (a minimised window, or no store) they are only taken.
         */
        Render::OverlayFrame ReadOverlay(Detail::RendererState& state, const AssetStore* store, bool draw)
        {
            Render::OverlayFrame frame;
            Overlay* overlay = state.app->TryGetResource<Overlay>();
            if (overlay == nullptr)
            {
                return frame;
            }
            std::vector<Detail::OverlaySubmissions::Taken> taken = Detail::OverlaySubmissions::Take(*overlay);
            if (!draw || store == nullptr)
            {
                return frame;
            }
            for (Detail::OverlaySubmissions::Taken& submission : taken)
            {
                if (submission.window != Window::MainWindow())
                {
                    if (state.loggedOverlayWindows.insert(submission.window.value).second)
                    {
                        state.app->Logger()->Warn(
                            "FenrirRenderer: a 2D draw list was submitted to the overlay of window "
                            "{}, which the renderer does not draw (only the main window), so it "
                            "is dropped",
                            submission.window.value);
                    }
                    continue;
                }
                const DrawList2D& list = submission.submission.list;
                for (const PipelineDescription2D& pipeline : list.pipelines)
                {
                    const ShaderData* data = store->TryGet(pipeline.shader);
                    if (data == nullptr)
                    {
                        continue;
                    }
                    const std::uint32_t revision = store->Revision(pipeline.shader);
                    if (!state.draw2D->IsShaderCurrent(KeyOf(pipeline.shader), revision))
                    {
                        state.draw2D->PrepareShader(KeyOf(pipeline.shader), revision, *data);
                    }
                }
                for (const Command2D& command : list.commands)
                {
                    for (const TextureSlot2D& slot : command.textures)
                    {
                        if (!slot.image.IsEmpty())
                        {
                            PrepareSampledImage(state, *store, slot.image, "a 2D draw list");
                        }
                    }
                }
                frame.submissions.push_back(std::move(submission.submission));
            }
            frame.contentScale = Window::Exists(*state.app, Window::MainWindow())
                                     ? Window::ContentScale(*state.app, Window::MainWindow())
                                     : 1.0f;
            return frame;
        }

        /** @brief One Render: the steps of ADR-0019, all on the main thread. */
        void RenderActiveScene(App& app, Detail::RendererState& state)
        {
            state.time += app.GetTime().deltaTime;
            const AssetStore* store = app.TryGetResource<AssetStore>();
            // Read the window size first: a minimised window builds nothing, but the frame index still advances.
            const Graphics::PixelSize size = state.surface->GetPixelSize();
            Render::SceneFrame frame;
            Render::DebugFrame debug;
            if (!size.IsEmpty() && store != nullptr)
            {
                Scene& scene = app.GetActiveScene();
                ReleaseStale(*state.scene, *state.draw2D, *store);
                const SceneSky sky = ReadSky(state, *store, scene.GetResource<Sky>(), "Sky");
                const SkyLight* skyLight = scene.GetResource<SkyLight>();
                const SceneSky light = skyLight != nullptr ? ReadSky(state, *store, &skyLight->sky, "SkyLight") : sky;
                frame.lighting = ReadLighting(state, scene, light);
                frame.backgroundIntensity = sky.intensity;
                frame.backgroundRotation = sky.rotation;
                frame.views = ReadViews(state, *store, scene, sky);
                ReadDraws(state, *store, scene, frame);
                debug = ReadDebug(state, scene);
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
            state.debug->SetFrame(std::move(debug));
            state.draw2D->SetFrame(ReadOverlay(state, store, !size.IsEmpty()));

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
                state.stats.paletteBytesUploaded = scene.paletteBytes;
                state.stats.distinctPalettesWritten = scene.palettes;
                state.stats.skinnedDraws = scene.skinnedDraws;
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
            // Only this state's resource: a later Install's must stay.
            const InstalledRenderer* installed = app == nullptr ? nullptr : app->TryGetResource<InstalledRenderer>();
            if (installed != nullptr && installed->state.expired())
            {
                app->RemoveResource<InstalledRenderer>();
                app->RemoveResource<Overlay>();
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
        // The window overlay's 2D draw lists sample the images the scene renderer prepares. Its Overlay pass is added
        // before the debug feature's, so debug screen text draws above every overlay layer (ADR-0027).
        auto draw2D = std::make_unique<Render::Draw2DFeature>(state->core->Device(), *app.Logger(), *scene);
        state->draw2D = draw2D.get();
        // Debug drawing goes into the views the scene renderer drew, so it is added after it.
        auto debug = std::make_unique<Render::DebugFeature>(state->core->Device(), *app.Logger(), *scene);
        state->debug = debug.get();
        state->core->AddFeature(std::move(scene));
        state->core->AddFeature(std::move(draw2D));
        state->core->AddFeature(std::move(debug));

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
        app.AddResource<Detail::InstalledRenderer>(Detail::InstalledRenderer{state});
        app.AddResource<Overlay>();
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
        if (!mesh->skin.has_value())
        {
            return Moved(mesh->bounds, GetWorldMatrix(entity));
        }
        // A skinned mesh (ADR-0035), from its skeleton's pose now; it is not drawn without a SkinnedMesh.
        SkinningSource* source = SkinningSourceOf(*m_state->app);
        if (source == nullptr || !entity.HasComponent<SkinnedMesh>())
        {
            return std::nullopt;
        }
        const Entity skeleton = entity.GetComponent<SkinnedMesh>().skeleton;
        Scene* scene = skeleton.IsValid() ? FindSceneOf(*m_state->app, skeleton) : nullptr;
        if (scene == nullptr || source->GetPoseState(*scene, skeleton) != PoseState::Ready)
        {
            return std::nullopt;
        }
        return SkinnedBounds(*mesh->skin, source->GetPalette(*scene, skeleton), GetWorldMatrix(skeleton));
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

    namespace
    {
        /**
         * @brief A second handle on `app`'s installed renderer state, for one call of a static: it shares the owner's
         * state, so it answers as the owner does, and ending it ends nothing while the owner lives. Invalid when the
         * app has no live owner.
         */
        std::shared_ptr<Detail::RendererState> InstalledState(App& app)
        {
            const Detail::InstalledRenderer* installed = app.TryGetResource<Detail::InstalledRenderer>();
            return installed == nullptr ? nullptr : installed->state.lock();
        }
    } // namespace

    bool Renderer::IsInstalled(App& app)
    {
        return InstalledState(app) != nullptr;
    }

    std::optional<Math::AABB> Renderer::GetWorldBounds(App& app, const Entity& entity)
    {
        return Renderer(InstalledState(app)).GetWorldBounds(entity);
    }

    RendererStats Renderer::Stats(App& app)
    {
        return Renderer(InstalledState(app)).Stats();
    }

    std::optional<Math::Ray> Renderer::ScreenPointToRay(App& app, const Entity& camera, Math::Vec2 point)
    {
        return Renderer(InstalledState(app)).ScreenPointToRay(camera, point);
    }

    std::optional<Math::Vec2> Renderer::WorldToScreenPoint(App& app, const Entity& camera, Math::Vec3 point)
    {
        return Renderer(InstalledState(app)).WorldToScreenPoint(camera, point);
    }

    Entity Renderer::CameraAt(App& app, WindowId window, Math::Vec2 point)
    {
        return Renderer(InstalledState(app)).CameraAt(window, point);
    }
} // namespace Fenrir
