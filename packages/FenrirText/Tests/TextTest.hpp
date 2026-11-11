#pragma once

#include <filesystem>
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

    // HarfBuzz's test fonts, read where the submodule keeps them: full fonts (subset/data/fonts) and small subsets
    // (api/fonts).
    inline std::filesystem::path FullFont(std::string_view name)
    {
        return std::filesystem::path(FENRIR_TEXT_HARFBUZZ_TEST) / "subset" / "data" / "fonts" / name;
    }

    inline std::filesystem::path SubsetFont(std::string_view name)
    {
        return std::filesystem::path(FENRIR_TEXT_HARFBUZZ_TEST) / "api" / "fonts" / name;
    }

    // An App with assets installed and a font library over its store. Members end in reverse order, so the library
    // ends before the store it uses.
    struct TextApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<RecordingLogger>(errors)};
        Fenrir::Assets assets;
        Fenrir::AssetStore& store;
        Fenrir::FontLibrary fonts;

        explicit TextApp(Fenrir::FontLibraryConfig config = {})
            : assets(Fenrir::Assets::Install(app, {})), store(assets.Store()), fonts(store, config)
        {
        }

        // Loads `file` as a font and registers it as `family`'s regular face.
        Fenrir::AssetHandle<Fenrir::Font> Register(std::string_view family, const std::filesystem::path& file)
        {
            const Fenrir::AssetHandle<Fenrir::Font> font =
                store.Load<Fenrir::Font>(file.string(), Fenrir::AssetOwner::App()).handle;
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
