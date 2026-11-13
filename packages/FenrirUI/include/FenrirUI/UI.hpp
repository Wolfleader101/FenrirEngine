#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirPainter/Painting.hpp"
#include "FenrirText/TextStyle.hpp"
#include "FenrirUI/Element.hpp"
#include "FenrirUI/UIRoot.hpp"

namespace Fenrir
{
    class App;
    class FontLibrary;

    /** @brief The UI's switches for every root (ADR-0063: each technique is a switch). */
    struct UISettings
    {
        /**
         * @brief Draw an element with an opacity under one and children through an offscreen group (ADR-0060), so
         * they fade as one. Off, each of its boxes is drawn at its own opacity instead, overlaps showing through.
         */
        bool offscreenGroups = true;
    };

    /**
     * @brief FenrirUI's default stylesheet, as CSS: embedded in the package from `Resources/default.css` and given to
     * every root made with UIRootConfig::defaultStylesheet, under the root's own sheets. A game that wants a look of
     * its own from the same start can copy it.
     */
    [[nodiscard]] std::string_view DefaultStylesheet() noexcept;

    /** @brief How a font face's glyphs are drawn (ADR-0062): hinted bitmaps by default, or a distance field. */
    enum class FontRendering : std::uint8_t
    {
        /** @brief Hinted grayscale bitmaps, one atlas entry per glyph per pixel size: crisp small text. */
        Bitmap,
        /** @brief A single-channel distance field at one size for every size: scaled and animated text. */
        Sdf,
    };

    /**
     * @brief A face of a font family as the UI registered it with the font family registry: from a stylesheet's
     * `@font-face` or from code (UIRoots::AddFontFace).
     */
    struct UIFontFace
    {
        std::string family;
        AssetHandle<Font> font;
        std::uint16_t weight = FontWeightNormal;
        FontStyle style = FontStyle::Normal;
        /**
         * @brief The face's opt-in to distance-field text, `font-rendering: sdf` in `@font-face`. Recorded here; the
         * text it draws is bitmaps until FenrirText has an SDF mode.
         */
        FontRendering rendering = FontRendering::Bitmap;
    };

    /** @brief What UI::Install is given. */
    struct UIConfig
    {
        UISettings settings;
    };

    /**
     * @brief The UI's app resource, which UI::Install adds: it makes and destroys the UI roots, which several may share
     * the app, ordered by layer; holds the registry of element tags, which starts with `panel` (a plain Element),
     * `label` and the controls' (Controls.hpp: `button`, `checkbox`, `slider`, `progress`, `image`, `scroll` and
     * `textentry`), and which a markup document's tags are looked up in; and holds the switches. Its frame system runs
     * every root's frame in PostUpdate, lowest layer first, roots of one layer in the order they were made.
     */
    class UIRoots
    {
      public:
        /** @brief The resource UI::Install adds, painting with `painterShader` (`builtin:painter`). */
        UIRoots(App& app, AssetHandle<Shader> painterShader, const UIConfig& config);
        UIRoots(const UIRoots&) = delete;
        UIRoots& operator=(const UIRoots&) = delete;
        UIRoots(UIRoots&&) = delete;
        UIRoots& operator=(UIRoots&&) = delete;
        ~UIRoots();

        /** @brief Makes a root. It lives until DestroyRoot or the resource ends. */
        UIRoot& CreateRoot(UIRootConfig config = {});
        /**
         * @brief Destroys `root` and its elements: at once it draws no more, is not among GetRoots and leaves
         * FenrirInput; it is freed on the next frame, so a UI listener may destroy the root that is calling it. False
         * when it is not one of these roots.
         */
        bool DestroyRoot(const UIRoot& root);
        /** @brief Every root, in the order they draw. */
        [[nodiscard]] std::vector<UIRoot*> GetRoots() const;

        /**
         * @brief Registers `factory` to make the elements of `tag` in every root, replacing any registered for it. A
         * custom element registers its subclass here; elements already made keep the class they were made with.
         */
        void RegisterElement(std::string_view tag, ElementFactory factory);
        /** @brief An element of `tag` from its registered factory, or a plain Element for an unregistered tag. */
        [[nodiscard]] std::unique_ptr<Element> MakeElement(std::string_view tag) const;
        /** @brief Whether a factory is registered for `tag`: a markup document may name only those. */
        [[nodiscard]] bool IsRegistered(std::string_view tag) const;

        /** @brief `builtin:painter`, which every root paints with. */
        [[nodiscard]] AssetHandle<Shader> GetPainterShader() const noexcept
        {
            return m_painterShader;
        }

        /**
         * @brief The fonts every root lays its text out with: the app's FontLibrary resource, which UI::Install adds
         * when the app has none. Register a family on it (FontLibrary::AddFace) for `font-family` to name. Null when
         * the app has none.
         */
        [[nodiscard]] FontLibrary* GetFonts() const noexcept;

        /**
         * @brief Registers `face` with the font family registry (FontLibrary::AddFace) and records it, replacing the
         * face of its family, weight and style registered before. A stylesheet's `@font-face` comes here when its sheet
         * is added. False, registering nothing, without a font library or with an empty font.
         */
        bool AddFontFace(const UIFontFace& face);
        /** @brief The faces registered through AddFontFace, in the order first registered. */
        [[nodiscard]] std::span<const UIFontFace> GetFontFaces() const noexcept;

        /** @brief Runs every root's frame, in drawing order; the PostUpdate system calls it. */
        void Frame();

        /** @brief The switches; a change takes effect on the next frame. */
        UISettings settings;

      private:
        struct RootEntry
        {
            std::unique_ptr<UIRoot> root;
            std::uint64_t order = 0;
        };

        App& m_app;
        AssetHandle<Shader> m_painterShader;
        std::vector<RootEntry> m_roots;
        /** @brief Roots destroyed since the last frame, freed at the start of the next. */
        std::vector<std::unique_ptr<UIRoot>> m_retired;
        std::uint64_t m_nextOrder = 0;
        std::uint32_t m_nextId = 1;
        std::unordered_map<std::string, ElementFactory> m_factories;
        std::vector<UIFontFace> m_fontFaces;
    };

    /**
     * @brief The owner of FenrirUI in an app: the UIRoots resource and the PostUpdate system that runs every root's
     * frame. Move-only; destroying it removes the resource, and with it every root.
     */
    class UI
    {
      public:
        /**
         * @brief Installs the painter (Painting::Install, so FenrirAssets' `Assets::Install` first), adds the UIRoots
         * resource and the sequential PostUpdate system that runs the roots' frames, and adds a FontLibrary resource
         * over the asset store when the app has none (the owner removes the one it added). A root on a window overlay
         * draws once the renderer is installed (it adds the overlay) and the window exists. Without an asset store, or
         * with a UI already installed, it logs why and returns an owner whose IsValid is false.
         */
        [[nodiscard]] static UI Install(App& app, UIConfig config = {});

        UI(UI&& other) noexcept;
        UI& operator=(UI&& other) noexcept;
        UI(const UI&) = delete;
        UI& operator=(const UI&) = delete;
        ~UI();

        /** @brief Whether Install added the resource: false if it failed, or once moved from. */
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_app != nullptr;
        }

      private:
        UI(App* app, std::optional<Painting> painting, bool ownsFonts) noexcept;

        /** @brief Removes the resources Install added. */
        void Uninstall() noexcept;

        App* m_app = nullptr;
        std::optional<Painting> m_painting;
        /** @brief Whether Install added the FontLibrary resource. */
        bool m_ownsFonts = false;
    };
} // namespace Fenrir
