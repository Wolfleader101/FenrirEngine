#pragma once

#include "FenrirUI/UIRoot.hpp"

// Where an `overflow: scroll` element's scrollbars are, shared by painting, hit testing and UIRoot::GetScrollbars so a
// press lands on the bar that was drawn.
namespace Fenrir::Detail
{
    struct ElementSlot;

    /**
     * @brief The element's scrollbars from its padding box, scroll offset and range and its `::scrollbar` styles: none
     * while it does not scroll or its bar has no width.
     */
    [[nodiscard]] ScrollbarBoxes ScrollbarsOf(const ElementSlot& slot);
} // namespace Fenrir::Detail
