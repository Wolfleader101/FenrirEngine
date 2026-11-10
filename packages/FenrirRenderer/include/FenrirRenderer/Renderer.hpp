#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "FenrirECS/Entity.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Frustum.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirRenderer/Components.hpp"

namespace Fenrir
{
    class App;
    struct RendererProbe;
    struct ScriptingExtension;

    namespace Detail
    {
        /** @brief What Renderer::Install set up. Defined in FenrirRenderer's sources; nothing else can use it. */
        struct RendererState;
    } // namespace Detail

    /** @brief The graphics backend the renderer draws with, picked at install (ADR-0016). */
    enum class GraphicsBackend : std::uint8_t
    {
        /** @brief OpenGL 4.6 core, on the main window's graphics surface. */
        OpenGL,
        /** @brief Draws nothing and needs no window or GPU: for tests and headless apps. */
        Null,
    };

    /** @brief How presenting waits for the display. */
    enum class Vsync : std::uint8_t
    {
        /** @brief Presents at once; frames may tear. */
        Off,
        /** @brief Waits for vertical sync. */
        On,
        /** @brief Waits for vertical sync unless the frame is late, which then tears instead of waiting. */
        Adaptive,
    };

    /** @brief RendererConfig::debugValidation's default: on in Debug builds, off in Release. */
    inline constexpr bool DebugValidationByDefault =
#ifdef NDEBUG
        false;
#else
        true;
#endif

    /** @brief How Renderer::Install sets the renderer up. There are no tuning settings and no native escape hatch. */
    struct RendererConfig
    {
        /** @brief The backend; one that cannot start fails Install, never falling back to another. */
        GraphicsBackend backend = GraphicsBackend::OpenGL;
        /** @brief Changeable later with Renderer::SetVsync. A mode the surface lacks falls back to `On`, logged. */
        Vsync vsync = Vsync::On;
        /**
         * @brief Samples per pixel of every view's scene targets, resolved in Output: 1 (off), 2, 4 or 8. Another
         * count uses the largest of those below it, logged.
         */
        std::uint32_t msaa = 1;
        /** @brief Whether the graphics API reports misuse to the app's logger; costly. On in Debug builds. */
        bool debugValidation = DebugValidationByDefault;
        /** @brief How many frames the GPU may still be drawing while the next is recorded (ADR-0019). */
        std::uint32_t framesInFlight = 2;
        /**
         * @brief Whether consecutive opaque and masked MeshRenderers with the same pipeline, material and mesh draw as
         * one instanced draw (ADR-0051); off draws each on its own, for comparison. Changeable later with
         * Renderer::SetAutomaticInstancing.
         */
        bool automaticInstancing = true;
        /**
         * @brief Whether each skinned mesh is skinned once a frame by a compute pass, which every pass then draws
         * rigidly (ADR-0050); off, every draw of it skins it in its vertex stage, under its shader's `SKINNED` variant
         * (ADR-0035). Changeable later with Renderer::SetComputeSkinning.
         */
        bool computeSkinning = true;
        /**
         * @brief Whether each camera's PointLights, SpotLights and RectLights are clustered (ADR-0049): a compute pass
         * a view lists them in 16 x 9 x 24 clusters of its frustum by range, and each pixel lights by its cluster's
         * lights only; off, every pixel loops over every light, for comparison. Changeable later with
         * Renderer::SetClusteredLights.
         */
        bool clusteredLights = true;
        /**
         * @brief Whether each camera's cull pass drops the opaque and masked copies outside its frustum on the GPU
         * (ADR-0057): each candidate's bounding sphere, then its box's eight corners, against the camera's planes;
         * off, the pass keeps every candidate, for comparison, and only the coarse cull of whole entities on the CPU
         * culls. Changeable later with Renderer::SetGpuCulling.
         */
        bool gpuCulling = true;
        /** @brief How many shadow cascades the DirectionalLight has per camera (ADR-0050): 1 to 4, 4 by default. */
        std::uint32_t shadowCascades = 4;
        /**
         * @brief The side of each cascade's shadow map, in texels: 2048 by default. Each camera's maps sit side by
         * side in one depth texture, so its width (this times shadowCascades) must be within the GPU's largest.
         */
        std::uint32_t shadowMapSize = 2048;
        /**
         * @brief How the cascades split the shadow distance (the practical scheme): 0 evenly, 1 logarithmically, so
         * the nearest cascades are smallest and sharpest. 0.75 by default.
         */
        float shadowSplitLambda = 0.75f;
        /**
         * @brief How many SpotLights may cast shadows in a frame (ADR-0050): 8 by default, the nearest to any camera
         * each frame; the rest light unshadowed, warned about once. 0 shadows none. Changeable later with
         * Renderer::SetSpotShadowBudget.
         */
        std::uint32_t spotShadowBudget = 8;
        /**
         * @brief The side of each shadowed spot light's tile of the spot shadow atlas, in texels: 1024 by default. The
         * atlas is a grid of the budget's tiles, ceil(sqrt(budget)) across, which must be within the GPU's largest.
         */
        std::uint32_t spotShadowMapSize = 1024;
        /**
         * @brief How many PointLights may cast shadows in a frame (ADR-0050): 4 by default, the nearest to any camera
         * each frame; the rest light unshadowed, warned about once. 0 shadows none. Changeable later with
         * Renderer::SetPointShadowBudget.
         */
        std::uint32_t pointShadowBudget = 4;
        /**
         * @brief The side of each face of each shadowed point light's cube, in texels: 512 by default. The cubes are
         * one cube array of the budget's cubes.
         */
        std::uint32_t pointShadowMapSize = 512;
    };

    /**
     * @brief What the renderer did in its last drawn frame (ADR-0023), except `instancesDrawn`, read back from the
     * most recent frame that has left flight (ADR-0057).
     */
    struct RendererStats
    {
        /**
         * @brief Draws of MeshRenderers in each view's Opaque and Transparent passes: one per run of matching opaque or
         * masked items merged into an instanced draw (ADR-0051), and one per transparent item.
         */
        std::uint32_t draws = 0;
        /**
         * @brief MeshRenderers drawn, one per item per view, however many draws they took, and the copies of each
         * InstancedMeshRenderer's instance list, one per copy per view.
         */
        std::uint32_t instances = 0;
        /**
         * @brief MeshRenderers and instance list copies the GPU drew after each camera's cull pass (ADR-0057),
         * summed over cameras, read back from the most recent frame that has left flight: usually two frames behind
         * `instances`, the candidates this frame submitted. 0 until a frame has been read back.
         */
        std::uint32_t instancesDrawn = 0;
        /** @brief Render passes run. */
        std::uint32_t passes = 0;
        /**
         * @brief Assets uploaded to the GPU by the prepare step: a mesh, a material's parameter block, or an instance
         * list.
         */
        std::uint32_t uploads = 0;
        /** @brief Bytes copied into the frame's palette buffer: every skeleton palette the frame's draws read. */
        std::uint32_t paletteBytesUploaded = 0;
        /**
         * @brief Skeleton palettes written, one per skeleton and pose however many meshes and views draw it
         * (ADR-0035).
         */
        std::uint32_t distinctPalettesWritten = 0;
        /** @brief Skinned MeshRenderers drawn, one per item per view; part of `instances`. */
        std::uint32_t skinnedDraws = 0;
        /**
         * @brief Draws into the sun's shadow cascades and the spot lights' shadow tiles (ADR-0050): one per run of
         * matching casting items, merged as in `draws`, per cascade of each view, per shadowed spot light, and per
         * shadowed point light (six times over for items whose shader draws its six faces one at a time).
         */
        std::uint32_t shadowDraws = 0;
    };

    /**
     * @brief The owner of FenrirRenderer in an app (ADR-0016, ADR-0019, ADR-0023): the renderer core and the graphics
     * device, and the Render-phase systems that draw the active scene's MeshRenderers through its Cameras every frame.
     * Move-only.
     *
     * Install FenrirAssets and FenrirWindow first, and destroy the Renderer before them and before the App (declaring
     * the owners in install order does this). Everything it does runs on the main thread, in the Render phase: call its
     * functions from the main thread too, outside parallel systems.
     */
    class Renderer
    {
      public:
        /**
         * @brief Turns rendering on for `app`: for OpenGL, requests the main window's graphics surface (an OpenGL 4.6
         * core context, a debug one with `debugValidation`, and an sRGB backbuffer) and starts the backend on it;
         * registers the renderer's built-in shaders in the asset store under `builtin:` names (`builtin:unlit`,
         * `builtin:lit`, `builtin:fallback`, `builtin:background`, `builtin:output`, the debug drawing programs,
         * `builtin:2d` and `builtin:panorama`); adds the Overlay app resource (ADR-0027), removed when the owner ends;
         * and adds the sequential Render system that draws each frame.
         *
         * Fails with one logged diagnostic, returning an owner whose IsValid() is false that added no systems, when the
         * app has no AssetStore, when the backend is not `Null` and the app has no valid Window (or the window was
         * already made), or when the backend cannot start. It never falls back to the null backend.
         */
        [[nodiscard]] static Renderer Install(App& app, RendererConfig config = {});

        Renderer(Renderer&& other) noexcept;
        Renderer& operator=(Renderer&& other) noexcept;
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        /** @brief Waits for the GPU, frees every GPU resource, and releases the main window's graphics surface. */
        ~Renderer();

        /** @brief True for an owner Install made with its backend running; false when Install failed, or once moved. */
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_state != nullptr;
        }

        /**
         * @brief Sets how presenting waits for the display, returning the mode now in effect: a mode the surface does
         * not support falls back to `On`, logged. An invalid owner does nothing and returns `Off`.
         */
        Vsync SetVsync(Vsync mode);

        /**
         * @brief Turns automatic instancing (RendererConfig::automaticInstancing, ADR-0051) on or off from the next
         * frame. An invalid owner does nothing.
         */
        void SetAutomaticInstancing(bool on);

        /**
         * @brief Turns compute skinning (RendererConfig::computeSkinning, ADR-0050) on or off from the next frame. An
         * invalid owner does nothing.
         */
        void SetComputeSkinning(bool on);

        /**
         * @brief Turns light clustering (RendererConfig::clusteredLights, ADR-0049) on or off from the next frame. An
         * invalid owner does nothing.
         */
        void SetClusteredLights(bool on);

        /**
         * @brief Turns the GPU frustum test (RendererConfig::gpuCulling, ADR-0057) on or off from the next frame. An
         * invalid owner does nothing.
         */
        void SetGpuCulling(bool on);

        /**
         * @brief Freezes culling from the next frame (ADR-0057): each camera then keeps culling, on the CPU and in its
         * cull pass, against the frustum it has in that frame, while it draws from wherever it moves to, so flying
         * out of the frozen volume shows what was kept; the Debug stage draws the frozen frustum's edges. Off, every
         * camera culls by where it is again. An invalid owner does nothing.
         */
        void SetCullingFrozen(bool on);

        /**
         * @brief Sets how many SpotLights may cast shadows (RendererConfig::spotShadowBudget, ADR-0050) from the next
         * frame, which makes the spot shadow atlas again at the new budget's size. An invalid owner does nothing.
         */
        void SetSpotShadowBudget(std::uint32_t budget);

        /**
         * @brief Sets how many PointLights may cast shadows (RendererConfig::pointShadowBudget, ADR-0050) from the
         * next frame, which makes the point shadow cubes again at the new budget's size. An invalid owner does
         * nothing.
         */
        void SetPointShadowBudget(std::uint32_t budget);

        /**
         * @brief The box around `entity`'s drawn mesh in world space, from the mesh's local bounds and the entity's
         * world matrix now. Empty if the entity is not alive, has neither a MeshRenderer nor an InstancedMeshRenderer,
         * its mesh does not resolve, or this owner is invalid.
         *
         * An InstancedMeshRenderer's (ADR-0057) is the box around every copy of its list's mesh box, under the
         * entity's world matrix: as the renderer computed it when it uploaded the list, or from the list now before
         * its first frame. Empty for a list that does not resolve or has no copies.
         *
         * A skinned mesh's (ADR-0035) comes from its skeleton's pose now: each bone's box moved by the bone's palette
         * matrix, with the box of its unweighted vertices, under the skeleton entity's world matrix. It is
         * conservative, not tight. Empty when the mesh has no SkinnedMesh, no skinning source is registered, or the
         * skeleton is not `Ready`.
         */
        [[nodiscard]] std::optional<Math::AABB> GetWorldBounds(const Entity& entity) const;

        /**
         * @brief What the last drawn frame did (`instancesDrawn` what the most recent frame out of flight drew);
         * zeros before the first and for an invalid owner.
         */
        [[nodiscard]] RendererStats Stats() const;

        /** @name Picking
         * Computed on call from the camera's world matrix and its render target's size now (ADR-0026), so they can be
         * used in Update. Points are in logical coordinates for a window (from its top-left corner) and in pixels for
         * an image. Each is empty for an invalid owner, an entity that is not alive or has no Camera, or a camera that
         * draws nothing (a window other than the main window, a stale or empty target image, a viewport under one
         * pixel); a disabled camera still answers.
         * @{
         */
        /**
         * @brief The ray from `camera` through `point`: a perspective camera's from its position, an orthographic
         * camera's from the point in its plane along its -Z. Empty for a point outside the camera's viewport.
         */
        [[nodiscard]] std::optional<Math::Ray> ScreenPointToRay(const Entity& camera, Math::Vec2 point) const;
        /** @brief Where `point` appears in `camera`'s target. Empty for a point behind the camera. */
        [[nodiscard]] std::optional<Math::Vec2> WorldToScreenPoint(const Entity& camera, Math::Vec3 point) const;
        /**
         * @brief The volume `camera` sees now (ADR-0057), as its Math::Frustum in world space from its world matrix
         * and its target's size: what the renderer culls its transparent items by, for a game that culls or streams
         * its own entities by it. Empty as ScreenPointToRay's is: a stale entity, no Camera, or a camera that draws
         * nothing.
         */
        [[nodiscard]] std::optional<Math::Frustum> GetFrustum(const Entity& camera) const;
        /**
         * @brief The camera drawn last at `point` of `window`: the highest-order enabled camera in the active scene
         * whose viewport holds it, of equal orders the highest entity id. A null Entity if there is none, or the
         * window is not the main window.
         */
        [[nodiscard]] Entity CameraAt(WindowId window, Math::Vec2 point) const;
        /** @} */

        /** @name The app's renderer
         * For code that does not hold the owner, such as FenrirRenderer's scripting extension: each answers as the
         * function of the same name of the owner Install made for `app`, while that owner lives and is valid, and as
         * an invalid owner otherwise. Call them from the main thread, as the owner's.
         * @{
         */
        /** @brief Whether `app` has a valid Renderer owner now. */
        [[nodiscard]] static bool IsInstalled(App& app);
        [[nodiscard]] static std::optional<Math::AABB> GetWorldBounds(App& app, const Entity& entity);
        [[nodiscard]] static RendererStats Stats(App& app);
        static void SetAutomaticInstancing(App& app, bool on);
        static void SetComputeSkinning(App& app, bool on);
        static void SetClusteredLights(App& app, bool on);
        static void SetGpuCulling(App& app, bool on);
        static void SetCullingFrozen(App& app, bool on);
        static void SetSpotShadowBudget(App& app, std::uint32_t budget);
        static void SetPointShadowBudget(App& app, std::uint32_t budget);
        [[nodiscard]] static std::optional<Math::Ray> ScreenPointToRay(App& app, const Entity& camera,
                                                                       Math::Vec2 point);
        [[nodiscard]] static std::optional<Math::Vec2> WorldToScreenPoint(App& app, const Entity& camera,
                                                                          Math::Vec3 point);
        [[nodiscard]] static std::optional<Math::Frustum> GetFrustum(App& app, const Entity& camera);
        [[nodiscard]] static Entity CameraAt(App& app, WindowId window, Math::Vec2 point);
        /** @} */

        /**
         * @brief FenrirRenderer's scripting extension (ADR-0029): the Fenrir.Renderer contract's components, resources
         * and native functions. Defined in the FenrirRendererScripting library, built only with FENRIR_ENABLE_RENDERER
         * and FENRIR_ENABLE_SCRIPTING: include "FenrirRendererScripting/RendererScripting.hpp" and link it to call it.
         * It needs no Renderer installed; its functions then do nothing.
         */
        [[nodiscard]] static Fenrir::ScriptingExtension ScriptingExtension();

      private:
        friend struct Fenrir::RendererProbe;

        explicit Renderer(std::shared_ptr<Detail::RendererState> state);

        // The Render system holds it weakly: once the owner is gone it does nothing.
        std::shared_ptr<Detail::RendererState> m_state;
    };
} // namespace Fenrir
