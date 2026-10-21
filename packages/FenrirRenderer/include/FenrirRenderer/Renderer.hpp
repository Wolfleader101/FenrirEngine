#pragma once

// SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirRenderer's public facade. Nothing here names GL, SDL, the
// graphics device or the renderer core.

#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Fenrir
{
    class App;

    /** @brief One drawn mesh (ADR-0023). */
    struct MeshRenderer
    {
        AssetHandle<MeshData> mesh;
        AssetHandle<MaterialData> material;
        bool visible = true;
    };

    /** @brief A view, oriented by its entity's world transform, looking along -Z (ADR-0026). Perspective only. */
    struct Camera
    {
        float fovY = 1.0471976f; // 60 degrees
        float near = 0.1f;
        int order = 0;
        bool enabled = true;
        float viewport[4] = {0, 0, 1, 1}; // normalised, from the top-left
        bool drawDebug = true;
        bool overrideClear = false; // clear to clearColour instead of the scene's Sky
        Colour clearColour{0, 0, 0, 1};
    };

    /** @brief Shines along its entity's -Z (ADR-0024). The first one in the active scene is used. */
    struct DirectionalLight
    {
        Colour colour{1, 1, 1, 1};
        float intensity = 1.0f;
    };

    /** @brief Scene resource. */
    struct AmbientLight
    {
        Colour colour{0.1f, 0.1f, 0.1f, 1};
        float intensity = 1.0f;
    };

    /** @brief Scene resource: a colour, or a cubemap if it has one (ADR-0026). */
    struct Sky
    {
        Colour colour{0.1f, 0.1f, 0.12f, 1};
        AssetHandle<ImageData> cubemap;
    };

    enum class GraphicsBackend
    {
        OpenGL,
        Null,
    };

    enum class VSync
    {
        Off,
        On,
        Adaptive,
    };

    struct RendererConfig
    {
        GraphicsBackend backend = GraphicsBackend::OpenGL;
        VSync vsync = VSync::On;
        bool debugValidation = true;
    };

    struct RendererStats
    {
        std::uint32_t items = 0;
        std::uint32_t draws = 0;
        std::uint32_t passes = 0;
        std::uint32_t culledPasses = 0;
        std::uint32_t uploads = 0;
        std::uint32_t pipelines = 0;
        std::uint32_t liveBuffers = 0;
        std::uint32_t liveTextures = 0;
        std::vector<std::string> passOrder;
    };

    namespace Detail
    {
        struct RendererState;
    }

    /**
     * @brief The owner of the renderer core and the graphics device. Move-only. Install FenrirAssets and (for any
     * backend but Null) FenrirWindow first; destroy it before them.
     */
    class Renderer
    {
      public:
        /** @brief On failure (no 4.6 context) logs the diagnostic and returns an owner whose IsValid is false. */
        [[nodiscard]] static Renderer Install(App& app, RendererConfig config = {});

        Renderer(Renderer&&) noexcept;
        Renderer& operator=(Renderer&&) noexcept;
        ~Renderer();

        [[nodiscard]] bool IsValid() const;
        void SetVSync(VSync vsync);
        [[nodiscard]] RendererStats Stats() const;
        /** @brief Axis-aligned world bounds from the mesh's local bounds and the world matrix (ADR-0023). */
        bool GetWorldBounds(const Entity& entity, Math::Vec3& min, Math::Vec3& max) const;
        /** @brief SPIKE: last frame's backbuffer, RGBA8, top row first. */
        std::vector<std::uint8_t> ReadBackbuffer(int& width, int& height);

      private:
        explicit Renderer(App* app);
        App* m_app = nullptr;
    };
} // namespace Fenrir
