#pragma once

#include <cstddef>
#include <cstdint>

#include "FenrirInput/InputRouter.hpp"
#include "FenrirInput/InputTypes.hpp"

namespace Fenrir::Detail
{
    /** @brief One window's claims in InputClaims: which window, and what the routers claim in it. */
    struct ScriptWindowClaims
    {
        WindowId window;
        WindowInputClaims claims;
    };

    /**
     * @brief The union of the input claims every input router publishes (ADR-0027), FenrirInput's InputClaims app
     * resource as C# reads it: per window, each claim is raised if any router raises it, and the caret is that of the
     * highest-priority router claiming text input; the gamepad claim is app-wide. Routers stay C++, so scripts read the
     * claims and never publish them.
     *
     * The view is of the claims as they were when it was got: get it again in a later system to see newer ones. It
     * holds up to MaxWindows windows; claims in more windows than that are left out, with a warning in a debug build.
     * With no input installed in the app there is none. InputClaims keeps its windows in a vector, which C# cannot read
     * in place, so FenrirInputScripting keeps this plain-data copy as an app resource of its own and refreshes it from
     * InputClaims whenever C# gets it.
     */
    struct ScriptInputClaims
    {
        /** @brief The windows C# can read claims for; the C# InputClaims.MaxWindows. */
        static constexpr std::size_t MaxWindows = 8;

        ScriptWindowClaims entries[MaxWindows] = {};
        std::uint32_t entryCount = 0;
        bool gamepadClaimed = false;
    };
} // namespace Fenrir::Detail
