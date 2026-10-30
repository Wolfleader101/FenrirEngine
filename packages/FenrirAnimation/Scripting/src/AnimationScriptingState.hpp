#pragma once

#include <string_view>

#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir
{
    /**
     * @brief The animation extension's state, which every animation native function is given second: the record of the
     * warnings it has logged, made by Animation::ScriptingExtension and kept by the Scripting given the extension. In a
     * header of its own because the generated natives header (Generated/FenrirAnimationNatives.hpp) declares the
     * natives, which name it.
     *
     * The texts the natives warn with are named here, one per rule, so the package's rules read in one place and its
     * rules tests name them; a clip or bone mask handle's rule is Assets' (AssetResolve.hpp).
     */
    struct AnimationScripting
    {
        /** @brief Warned by a clip event read or removal whose index is not one of the clip's events. */
        static constexpr std::string_view NoSuchEvent = "the clip has no event at that index, so it does nothing";

        /** @brief Warned by BoneMasks.Create given an owner that is neither the scene nor the app. */
        static constexpr std::string_view NoOwner = "the owner is neither Scene nor App, so it does nothing";

        /**
         * @brief Warned when the asset store refuses an edit (a time outside the clip, a weight that is not finite):
         * the store logs why, and the native returns false.
         */
        static constexpr std::string_view Refused = "the asset store refused the edit (logged), so it does nothing";

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"animation scripting"};
    };
} // namespace Fenrir
