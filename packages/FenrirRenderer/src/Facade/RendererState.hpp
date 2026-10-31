#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "Core/DebugFeature.hpp"
#include "Core/Draw2DFeature.hpp"
#include "Core/RendererCore.hpp"
#include "Core/SceneRenderer.hpp"
#include "Device/GraphicsSurface.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirRenderer/Renderer.hpp"

namespace Fenrir
{
    class App;
}

namespace Fenrir::Detail
{
    /**
     * @brief The main window's graphics surface for the OpenGL backend, through FenrirWindow's surface functions
     * (ADR-0022). A minimised window reads as an empty size, so nothing is drawn to it.
     */
    class WindowSurface final : public Graphics::GraphicsSurface
    {
      public:
        explicit WindowSurface(App& app) : m_app(&app)
        {
        }

        [[nodiscard]] Graphics::PixelSize GetPixelSize() const override;
        [[nodiscard]] void* GetProcAddress(const char* name) override;
        bool MakeCurrent() override;
        void Present() override;
        bool SetSwapInterval(int interval) override;

      private:
        App* m_app;
    };

    /**
     * @brief The surface the null backend draws into: the main window's pixel size while it exists (empty while it is
     * minimised), else a fixed size. It presents nothing.
     */
    class NullSurface final : public Graphics::GraphicsSurface
    {
      public:
        /** @brief The size with no window, the default main window's. */
        static constexpr Graphics::PixelSize FixedSize{1280, 720};

        explicit NullSurface(App& app) : m_app(&app)
        {
        }

        /** @brief A size RendererProbe sets to stand in for a resized or minimised window; none by default. */
        std::optional<Graphics::PixelSize> sizeOverride;

        [[nodiscard]] Graphics::PixelSize GetPixelSize() const override;
        [[nodiscard]] void* GetProcAddress(const char*) override
        {
            return nullptr;
        }
        bool MakeCurrent() override
        {
            return true;
        }
        void Present() override
        {
        }
        bool SetSwapInterval(int) override
        {
            return true;
        }

      private:
        App* m_app;
    };

    /** @brief What Renderer::Install set up, shared by the owner and held weakly by the Render system. */
    struct RendererState
    {
        App* app = nullptr;
        RendererConfig config;
        /** @brief Whether the main window's surface was requested, and so is released at teardown. */
        bool holdsSurface = false;
        std::unique_ptr<Graphics::GraphicsSurface> surface;
        std::unique_ptr<Render::RendererCore> core;
        /** @brief The core's scene render feature, which the core owns. */
        Render::SceneRenderer* scene = nullptr;
        /** @brief The core's debug render feature, which the core owns. */
        Render::DebugFeature* debug = nullptr;
        /** @brief The core's 2D render feature, which draws the window overlay; the core owns it. */
        Render::Draw2DFeature* draw2D = nullptr;
        RendererStats stats;
        /** @brief Seconds of frame time since the first Render. */
        double time = 0.0;

        /** @name Logged once
         * @{
         */
        StaleHandleLog staleMeshes;
        StaleHandleLog fallbackMaterials;
        bool loggedNoCamera = false;
        bool loggedLights = false;
        /** @brief Cameras that draw nothing, by entity id and why. */
        std::set<std::pair<std::uint32_t, std::uint8_t>> loggedCameras;
        /** @brief Cameras of equal order into one target, by their entity ids. */
        std::set<std::pair<std::uint32_t, std::uint32_t>> loggedTies;
        /** @brief Images materials name that cannot be sampled as a 2D texture, by index and generation. */
        std::set<std::pair<std::uint32_t, std::uint32_t>> loggedImages;
        /** @brief Sky cubemaps and panoramas that cannot be drawn: empty, stale, or the wrong shape. */
        StaleHandleLog skyImages;
        /** @brief Windows the renderer does not draw that overlay submissions named, by id. */
        std::set<std::uint32_t> loggedOverlayWindows;
        /** @brief Whether a skinned draw found no skinning source registered. */
        bool loggedNoSkinningSource = false;
        /** @brief Mesh entities whose skinning was skipped or ignored, by entity id and why (SkinProblem). */
        std::set<std::pair<std::uint32_t, std::uint8_t>> loggedSkins;
        /** @} */

        /**
         * @brief A mesh entity's last compatibility verdict against its skeleton (ADR-0035), kept while the mesh and
         * skeleton handles and revisions it was reached for hold, so it is checked only when the pairing changes.
         */
        struct SkinPairing
        {
            AssetHandle<Mesh> mesh;
            std::uint32_t meshRevision = 0;
            AssetHandle<Skeleton> skeleton;
            std::uint32_t skeletonRevision = 0;
            bool compatible = false;

            [[nodiscard]] bool SameAs(const SkinPairing& other) const noexcept
            {
                return mesh == other.mesh && meshRevision == other.meshRevision && skeleton == other.skeleton &&
                       skeletonRevision == other.skeletonRevision;
            }
        };
        /** @brief By mesh entity id; entities not drawn in a frame are dropped. */
        std::map<std::uint32_t, SkinPairing> skinPairings;

        RendererState() = default;
        RendererState(const RendererState&) = delete;
        RendererState& operator=(const RendererState&) = delete;
        RendererState(RendererState&&) = delete;
        RendererState& operator=(RendererState&&) = delete;
        /** @brief Frees the GPU side, releases the main window's surface, and removes its InstalledRenderer. */
        ~RendererState();
    };

    /** @brief How the renderer takes the Overlay's submissions each Render. */
    struct OverlaySubmissions
    {
        /** @brief One submission: the window it names, and its layer and list. */
        struct Taken
        {
            WindowId window;
            Render::Submission2D submission;
        };

        /** @brief Every submission made since the last call, in submission order, leaving none. */
        static std::vector<Taken> Take(Overlay& overlay)
        {
            std::vector<Overlay::Submission> submissions;
            {
                const std::lock_guard<std::mutex> lock(overlay.m_mutex);
                submissions.swap(overlay.m_submissions);
            }
            std::vector<Taken> taken;
            taken.reserve(submissions.size());
            for (Overlay::Submission& submission : submissions)
            {
                taken.push_back({submission.window, {submission.layer, std::move(submission.list)}});
            }
            return taken;
        }
    };

    /** @brief The app resource through which Renderer's statics find the installed renderer's state. */
    struct InstalledRenderer
    {
        std::weak_ptr<RendererState> state;
    };
} // namespace Fenrir::Detail
