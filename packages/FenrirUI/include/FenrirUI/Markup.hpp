#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Stylesheet.hpp"

namespace Fenrir
{
    /** @brief Something a markup document's load refused, and where: the document or sheet, and the line (from 1). */
    struct MarkupError
    {
        /** @brief The name of the document or sheet it is in: the loaded one's, or the src of an include or style. */
        std::string file;
        std::uint32_t line = 0;
        std::string message;
    };

    /**
     * @brief What UIRoot::LoadMarkup made: the elements at the document's top level (an include's there among them),
     * the sheets its style blocks attached, and every error. A document with errors loads what it could.
     */
    struct MarkupLoad
    {
        /** @brief The first element at the document's top level; nothing when none loaded. */
        ElementId element;
        /** @brief Every element at the document's top level, in order. */
        std::vector<ElementId> elements;
        /** @brief The sheets its `<style>` blocks attached to the root, in order. */
        std::vector<StylesheetId> stylesheets;
        /** @brief What it refused, in document order (an included document's errors where its include is). */
        std::vector<MarkupError> errors;
    };

    /**
     * @brief Gives the text of the document or sheet an `<include src>` or `<style src>` names, by its src as written;
     * nothing when there is none.
     */
    using MarkupResolver = std::function<std::optional<std::string>(std::string_view src)>;
} // namespace Fenrir
