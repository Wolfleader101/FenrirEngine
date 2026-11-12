#pragma once

#include "FenrirDraw2D/DrawList2D.hpp"

namespace Fenrir::Detail
{
    struct UIRootState;

    /**
     * @brief Paints the root's laid-out tree into `list` through the painter, in tree order: each element's shadows,
     * background and border, its content, then its children by z-index inside its overflow clip, under its transform
     * and, when its opacity is under one and it has children, inside an offscreen group.
     */
    void PaintRoot(UIRootState& state, DrawList2D& list, float contentScale);
} // namespace Fenrir::Detail
