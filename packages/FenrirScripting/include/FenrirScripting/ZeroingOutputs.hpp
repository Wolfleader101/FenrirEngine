#pragma once

// The registration zeroes a native function's single outputs (ADR-0045): the generated registration names
// `ZeroingOutputs<&Native, indices...>::Call` where it would name the native, so every `out` parameter holds its
// default before the native runs, and a native that returns early writes nothing.

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Fenrir
{
    namespace Detail
    {
        template <auto Native, typename Pointer, std::size_t... Outputs>
        struct ZeroingOutputsCall;

        template <auto Native, typename R, typename... Args, std::size_t... Outputs>
        struct ZeroingOutputsCall<Native, R (*)(Args...), Outputs...>
        {
            static R Call(Args... arguments)
            {
                [[maybe_unused]] const std::tuple<Args&...> all(arguments...);
                (Zero(std::get<Outputs>(all)), ...);
                return Native(std::forward<Args>(arguments)...);
            }

          private:
            template <typename T>
            static void Zero(T* output)
            {
                static_assert(!std::is_const_v<T>, "an output index names a pointer to const, which is an input");
                *output = T{};
            }
        };

        template <auto Native, typename R, typename... Args, std::size_t... Outputs>
        struct ZeroingOutputsCall<Native, R (*)(Args...) noexcept, Outputs...>
            : ZeroingOutputsCall<Native, R (*)(Args...), Outputs...>
        {
        };
    } // namespace Detail

    /**
     * @brief `Native` with the pointer parameters at `Outputs` (indices into its full parameter list, the call context
     * and extension state counted) assigned their type's default before it runs. `Call` has the native function's
     * signature, so it is what `NativeFunction::Make` is given in place of the native, and the zeroing happens inside
     * the not-running check: a call while no Scripting runs returns the not-running value and touches no output.
     *
     * For each single `out` parameter; an output span is governed by its returned count, and an `inOut` parameter is
     * C#'s value, so neither is named.
     *
     * ```cpp
     * NativeFunction::Make<&ZeroingOutputs<&PhysicsNatives::Raycast, 6>::Call>("Fenrir.Physics.PhysicsNative.Raycast")
     * ```
     */
    template <auto Native, std::size_t... Outputs>
    struct ZeroingOutputs : Detail::ZeroingOutputsCall<Native, decltype(Native), Outputs...>
    {
    };
} // namespace Fenrir
