#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirTextScripting/ScriptText.hpp"

namespace Fenrir
{
    /**
     * @brief The text extension's state, which every text native function is given second: the paragraphs scripts
     * made, by id, made by Text::ScriptingExtension and kept by the Scripting given the extension. Public, because
     * FenrirPainter's extension shares it (ScriptingExtension::GetState) to draw those paragraphs, and in a header of
     * its own because the generated registration declares the natives, which name it. The text the natives warn with is
     * named here, so the package's rules read in one place and its rules tests name them.
     */
    struct TextScripting
    {
        /** @brief Warned when the app has no FontLibrary resource: the native does nothing. */
        static constexpr std::string_view NoFontLibrary =
            "the app has no FontLibrary resource (add one over its AssetStore), so it does nothing";
        /** @brief Warned when the app has no AssetStore: the native loads no font. */
        static constexpr std::string_view NoAssetStore =
            "the app has no AssetStore (FenrirAssets is not installed), so it loads no font";
        /** @brief Warned when the font file does not load: the native registers no face. */
        static constexpr std::string_view FontNotLoaded = "the font did not load, so it registers no face";
        /** @brief Warned for a paragraph that was destroyed or never made: the native does nothing. */
        static constexpr std::string_view NoParagraph = "the paragraph was destroyed or never made, so it does nothing";

        /**
         * @brief The paragraph `paragraph` names, or null, warning once under the calling native's name, when it was
         * destroyed or never made.
         */
        Fenrir::Paragraph* Find(const CallContext& context, const ScriptText::Paragraph& paragraph)
        {
            const NativeGuard guard(context, warnings);
            const auto found = paragraphs.find(paragraph.id);
            return guard.Require(found == paragraphs.end() ? nullptr : &found->second, std::string(NoParagraph));
        }

        /** @brief The app's FontLibrary, or null, warning once under the calling native's name, when it has none. */
        FontLibrary* Fonts(const CallContext& context)
        {
            const NativeGuard guard(context, warnings);
            return guard.Require(context.GetApp().TryGetResource<FontLibrary>(), std::string(NoFontLibrary));
        }

        /** @brief The paragraphs scripts made and have not destroyed, by id. Node-based, so each stays where it is. */
        std::unordered_map<std::uint64_t, Fenrir::Paragraph> paragraphs;
        /** @brief The id the next paragraph gets: ids are never handed out again. */
        std::uint64_t nextId = 1;
        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"text scripting"};
    };
} // namespace Fenrir
