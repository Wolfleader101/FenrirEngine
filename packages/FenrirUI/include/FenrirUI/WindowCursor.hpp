#pragma once

#include "FenrirUI/Style.hpp"
#include "FenrirWindow/WindowTypes.hpp"

namespace Fenrir
{
    /**
     * @brief The window's cursor shape for a cursor UIRoot::GetHoveredCursor gives, as a root on the window sets it
     * (UIRootConfig::applyCursor): `auto` and `default` the arrow, `pointer` the hand, `text` the I-beam, `move`,
     * `not-allowed` and the four resizes their shapes, `none` hidden. For a game applying a world root's cursor itself;
     * FenrirUI links FenrirWindow privately, so a caller of this links FenrirWindow too.
     */
    [[nodiscard]] CursorShape WindowCursorShape(Cursor cursor) noexcept;
} // namespace Fenrir
