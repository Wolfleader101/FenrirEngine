#pragma once

#include "FenrirAssets/AssetHandle.hpp"

namespace Fenrir
{
    class App;
    // Only named here: FenrirPainterScripting defines Painting::ScriptingExtension().
    struct ScriptingExtension;

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

        /**
         * @brief The painter's scripting extension (ADR-0061): a C# system is given a painter over a window's overlay
         * for one layer (Painter.Overlay), makes paths, and paints through the Painter facade, one native per call; it
         * draws the paragraphs of `text`, the text extension (Text::ScriptingExtension), whose state it shares.
         *
         * Defined by the FenrirPainterScripting library, not FenrirPainter (which never links FenrirScripting), so it
         * exists only in a build with scripting on. Include "FenrirPainterScripting/PainterScripting.hpp" and link
         * FenrirPainterScripting to call it.
         */
        [[nodiscard]] static Fenrir::ScriptingExtension ScriptingExtension(const Fenrir::ScriptingExtension& text);

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
