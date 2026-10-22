#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "FenrirMath/Math.hpp"
#include "FenrirRenderer/Components.hpp"

namespace Fenrir
{
    class App;
    class Entity;
    struct RendererProbe;

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
        /** @brief Samples per pixel of the scene targets; 1 is off. Kept, but not yet drawn with (renderer ticket 14).
         */
        std::uint32_t msaa = 1;
        /** @brief Whether the graphics API reports misuse to the app's logger; costly. On in Debug builds. */
        bool debugValidation = DebugValidationByDefault;
        /** @brief How many frames the GPU may still be drawing while the next is recorded (ADR-0019). */
        std::uint32_t framesInFlight = 2;
    };

    /** @brief What the renderer did in its last drawn frame (ADR-0023). */
    struct RendererStats
    {
        /** @brief Draws of MeshRenderers, one per item per view. */
        std::uint32_t draws = 0;
        /** @brief Render passes run. */
        std::uint32_t passes = 0;
        /** @brief Assets uploaded to the GPU by the prepare step: a mesh, or a material's parameter block. */
        std::uint32_t uploads = 0;
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
         * `builtin:lit`, `builtin:fallback`, `builtin:background`, `builtin:output`); and adds the sequential Render
         * system that draws each frame.
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
         * @brief The box around `entity`'s drawn mesh in world space, from the mesh's local bounds and the entity's
         * world matrix now. Empty if the entity is not alive, has no MeshRenderer, its mesh does not resolve, or this
         * owner is invalid.
         */
        [[nodiscard]] std::optional<Math::AABB> GetWorldBounds(const Entity& entity) const;

        /** @brief What the last drawn frame did; zeros before the first and for an invalid owner. */
        [[nodiscard]] RendererStats Stats() const;

      private:
        friend struct Fenrir::RendererProbe;

        explicit Renderer(std::shared_ptr<Detail::RendererState> state);

        // The Render system holds it weakly: once the owner is gone it does nothing.
        std::shared_ptr<Detail::RendererState> m_state;
    };
} // namespace Fenrir
