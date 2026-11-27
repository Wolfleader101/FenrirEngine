#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"

namespace TextTest
{
    // Keeps what was logged as errors, so a test can check nothing went wrong. A second ConsoleLogger in one process
    // throws, so the tests never use one.
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<std::vector<std::string>> errors) : m_errors(std::move(errors))
        {
        }
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view) override
        {
        }
        void ErrorImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_errors;
    };

    // HarfBuzz's test fonts, by their asset paths under TextApp's asset root, the submodule's test directory: full
    // fonts (subset/data/fonts) and small subsets (api/fonts).
    inline std::string FullFont(std::string_view name)
    {
        return "subset/data/fonts/" + std::string(name);
    }

    inline std::string SubsetFont(std::string_view name)
    {
        return "api/fonts/" + std::string(name);
    }

    // An App with assets installed over HarfBuzz's test directory and a font library over its store. Members end in
    // reverse order, so the library ends before the store it uses.
    struct TextApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<RecordingLogger>(errors)};
        Fenrir::Assets assets;
        Fenrir::AssetStore& store;
        Fenrir::FontLibrary fonts;

        explicit TextApp(Fenrir::FontLibraryConfig config = {})
            : assets(Fenrir::Assets::Install(app, {FENRIR_TEXT_HARFBUZZ_TEST})), store(assets.Store()),
              fonts(store, config)
        {
        }

        // Loads the font at asset path `path` and registers it as `family`'s regular face.
        Fenrir::AssetHandle<Fenrir::Font> Register(std::string_view family, std::string_view path)
        {
            const Fenrir::AssetHandle<Fenrir::Font> font =
                store.Load<Fenrir::Font>(path, Fenrir::AssetOwner::App()).handle;
            fonts.AddFace(family, font);
            return font;
        }
    };

    // `text` as the UTF-8 a std::string holds: u8 literals are UTF-8 whatever the compiler's execution character set.
    inline std::string Utf8(std::u8string_view text)
    {
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    // A style in `family` at `size` pixels.
    inline Fenrir::TextStyle Style(std::string family, float size = 16.0f)
    {
        Fenrir::TextStyle style;
        style.family = std::move(family);
        style.size = size;
        return style;
    }

    // Every glyph of every run, in run order, so left to right within a line.
    inline std::vector<Fenrir::PositionedGlyph> Glyphs(const Fenrir::Paragraph& paragraph)
    {
        std::vector<Fenrir::PositionedGlyph> glyphs;
        for (const Fenrir::GlyphRun& run : paragraph.Runs())
        {
            glyphs.insert(glyphs.end(), run.glyphs.begin(), run.glyphs.end());
        }
        return glyphs;
    }
} // namespace TextTest
