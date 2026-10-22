#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "FenrirInput/InputRouter.hpp"
#include "FenrirInput/InputTypes.hpp"

namespace Fenrir::Detail
{
    /** @brief One window's claims in ScriptInputClaims: the C# WindowClaimsEntry. */
    struct ScriptWindowClaims
    {
        WindowId window;
        WindowInputClaims claims;
    };

    /**
     * @brief What C# reads as the InputClaims resource (the C# Fenrir.Input.InputClaims): InputClaims as plain data,
     * with room for MaxWindows windows. InputClaims keeps its windows in a vector, which C# cannot read in place, so
     * FenrirInputScripting keeps this as an app resource of its own and refreshes it from InputClaims whenever C# gets
     * it.
     */
    struct ScriptInputClaims
    {
        /** @brief The windows C# can read claims for; the C# InputClaims.MaxWindows. */
        static constexpr std::size_t MaxWindows = 8;

        std::array<ScriptWindowClaims, MaxWindows> windows{};
        std::uint32_t windowCount = 0;
        bool gamepad = false;
    };
} // namespace Fenrir::Detail
