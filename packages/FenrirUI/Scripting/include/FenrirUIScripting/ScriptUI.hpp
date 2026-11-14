#pragma once

// What FenrirUI's natives take that FenrirUI's own headers cannot share as plain data: the settings a script makes a UI
// root with, since UIRootConfig holds its target as a shared pointer and its blend space as an optional. The natives
// turn it into a UIRootConfig. This header is the UI binding's own API description
// (Scripting/api/FenrirUIScripting.api.toml), with FenrirUI's plain data it names (ElementId, UIRootId, UIEvent,
// ElementBoxes and the enums), so their C# mirrors and mappings are generated from it and checked against C# as scripts
// load. The doc comments are C#'s.

#include <cstdint>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirUI/UIRoot.hpp"

namespace Fenrir::ScriptUI
{
    /**
     * @brief What a script makes a UI root with (UIRoot.Create): a root on a window's overlay at a layer, blending in
     * the overlay's gamma space. A root drawn into an image or the world is made in C++.
     */
    struct RootSettings
    {
        /** @brief The window whose overlay it draws on. */
        WindowId window{1};
        /** @brief The overlay layer it draws on; roots draw lowest layer first, then in the order they were made. */
        std::int32_t layer = 0;
        /** @brief How its UI units map to the window's logical units. */
        ScaleRule scaleRule = ScaleRule::Height1080;
        /** @brief Snap laid-out rectangles to whole pixels. */
        bool snapToPixels = true;
        /**
         * @brief Its input router's priority: routers run highest first, so give a root drawn over another a higher
         * one.
         */
        std::int32_t inputPriority = 0;
        /** @brief Give it FenrirUI's default stylesheet before its own sheets. */
        bool defaultStylesheet = true;
        /** @brief Start with the virtual cursor on: the left stick moves the pointer and A clicks under it. */
        bool virtualCursor = false;
        /** @brief Start with hot reload on: the files it loads are polled for changes. */
        bool hotReload = false;
    };
} // namespace Fenrir::ScriptUI
