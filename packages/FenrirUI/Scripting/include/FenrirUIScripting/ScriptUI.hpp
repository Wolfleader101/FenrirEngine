#pragma once

// What FenrirUI's natives take that FenrirUI's own headers cannot share as plain data: the settings a script makes a UI
// root with, on a window, in an image or on a world quad, since UIRootConfig holds its target as a shared pointer and
// its blend space as an optional. The natives turn each into a UIRootConfig, making the image or world target, which
// the root holds and ends with it. This header is the UI binding's own API description
// (Scripting/api/FenrirUIScripting.api.toml), with FenrirUI's plain data it names (ElementId, UIRootId, UIEvent,
// ElementBoxes and the enums), so their C# mirrors and mappings are generated from it and checked against C# as scripts
// load. The doc comments are C#'s.

#include <cstdint>

#include "FenrirAssets/ImageData.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirUI/UIRoot.hpp"

namespace Fenrir::ScriptUI
{
    /**
     * @brief What a script makes a UI root with (UIRoot.Create): a root on a window's overlay at a layer, blending in
     * the overlay's gamma space. A root drawn into an image is made with ImageRootSettings, and one on a quad in the
     * world with WorldRootSettings.
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

    /**
     * @brief What a script makes a UI root in an image with (UIRoot.CreateImageRoot): an image asset the root owns, of
     * the size given, that it draws into each frame, blending linear, at a content scale of 1, so a root on the
     * `Height1080` scale rule spans 1080 UI units of the image's height. Its input comes from code: it takes no
     * window input. The image (UIRoot.GetImage) is unloaded when the root ends.
     */
    struct ImageRootSettings
    {
        /** @brief The image's width in pixels; with a zero width or height the root draws nothing. */
        std::uint32_t width = 0;
        /** @brief The image's height in pixels. */
        std::uint32_t height = 0;
        /** @brief How the image's colours are encoded: sRGB for a colour texture. */
        ColourSpace colourSpace = ColourSpace::Srgb;
        /** @brief How its UI units map to the image's pixels. */
        ScaleRule scaleRule = ScaleRule::Height1080;
        /** @brief Snap laid-out rectangles to whole pixels. */
        bool snapToPixels = true;
        /** @brief Give it FenrirUI's default stylesheet before its own sheets. */
        bool defaultStylesheet = true;
        /** @brief Start with hot reload on: the files it loads are polled for changes. */
        bool hotReload = false;
    };

    /**
     * @brief What a script makes a UI root on a quad in the world with (UIRoot.CreateWorldRoot): the entity's
     * WorldQuad2D, whose size in world units and UI units per world unit it reads each frame, drawn among the scene's
     * transparent items, blending linear. The game keeps the entity while the root lives. Its pointer is the ray the
     * script gives it each frame (UIRoot.SetPointerRay); it takes the window's keys, text and gamepads.
     */
    struct WorldRootSettings
    {
        /** @brief The entity of the active scene whose WorldQuad2D it draws on. */
        Entity entity;
        /** @brief How its UI units map to the quad's: one to one by default, the quad's own units. */
        ScaleRule scaleRule = ScaleRule::ContentScale;
        /** @brief Snap laid-out rectangles to whole pixels. */
        bool snapToPixels = true;
        /** @brief Give it FenrirUI's default stylesheet before its own sheets. */
        bool defaultStylesheet = true;
        /** @brief Start with hot reload on: the files it loads are polled for changes. */
        bool hotReload = false;
        /** @brief Start with the virtual cursor on: the left stick moves the pointer and A clicks under it. */
        bool virtualCursor = false;
    };
} // namespace Fenrir::ScriptUI
