#pragma once

#include "FenrirAnimation/Animation.hpp"

namespace Fenrir
{
    namespace Detail
    {
        /** @brief What one `Animation` install holds, shared with its sampler, which holds it weakly. */
        struct AnimationState
        {
            AnimationState(App& installedIn, AnimationConfig installedWith) noexcept
                : app(&installedIn), config(installedWith)
            {
            }

            App* app;
            AnimationConfig config;
        };

        /** @brief The app resource marking FenrirAnimation installed, so a second install is refused. */
        struct AnimationInstalled
        {
            const AnimationState* state = nullptr;
        };
    } // namespace Detail
} // namespace Fenrir
