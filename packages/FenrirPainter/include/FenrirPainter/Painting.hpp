#pragma once

#include "FenrirAssets/AssetHandle.hpp"

namespace Fenrir
{
    class App;

    /**
     * @brief The owner of what Painting::Install set up: the painter's shader, `builtin:painter`, registered with the
     * app's asset store (ADR-0027 lets a UI bring its own shaders). Move-only. The shader stays registered for the
     * asset store's life; the owner only hands out its handle.
     */
    class Painting
    {
      public:
        /**
         * @brief Registers `builtin:painter` with `app`'s asset store (FenrirAssets' `Assets::Install` first). With no
         * asset store, or a shader the store refuses, it logs why and returns an owner whose IsValid is false.
         */
        [[nodiscard]] static Painting Install(App& app);

        Painting(Painting&& other) noexcept;
        Painting& operator=(Painting&& other) noexcept;
        Painting(const Painting&) = delete;
        Painting& operator=(const Painting&) = delete;
        ~Painting() = default;

        /** @brief Whether the shader was registered: false if Install failed, or once moved from. */
        [[nodiscard]] bool IsValid() const noexcept;

        /** @brief `builtin:painter`'s handle, for PainterConfig::shader; empty when not valid. */
        [[nodiscard]] AssetHandle<Fenrir::Shader> GetShader() const noexcept;

      private:
        explicit Painting(AssetHandle<Fenrir::Shader> shader) noexcept;

        AssetHandle<Fenrir::Shader> m_shader;
    };
} // namespace Fenrir
