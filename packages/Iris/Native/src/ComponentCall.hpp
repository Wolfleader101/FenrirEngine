#pragma once

#include "BridgeFunctions.hpp"
#include "Iris/Object.hpp"
#include "ManagedDiagnosticBuffer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Iris::Detail
{
    // The rules of a call that belongs to one component, written once for the method, member and array handles:
    // that an object they are given is valid and belongs to the component, and that a block a call returns is given
    // back however the call ends. (Taking a returned object, string or struct is CallOrigin.)
    class ComponentCall
    {
      public:
        // Why an object cannot be used, or nothing: it is released or moved from (InvalidHandle), or belongs to
        // another component (InvalidArgument). `argument` is the object's position among a call's arguments; without
        // it the object is the call's target and the explanation says "the object".
        [[nodiscard]] static std::optional<std::pair<CallStatus, std::string>> ObjectProblem(
            const Object& object, std::uint64_t component, std::optional<std::size_t> argument);

        // Checks the object arguments of a call and writes their handles into `slots`. `objects[i]` is the Object for
        // argument i, or null when `slots[i]` already holds the value.
        [[nodiscard]] static std::optional<std::pair<CallStatus, std::string>> ResolveObjects(
            std::uint64_t component, std::span<std::uint64_t> slots, std::span<const Object* const> objects);

        // The handle of a call's target (zero for none), or why it cannot be used.
        [[nodiscard]] static std::optional<std::pair<CallStatus, std::string>> TargetHandle(std::uint64_t component,
                                                                                            const Object* target,
                                                                                            std::uint64_t& handle);

        // Completes a call whose result slot is a block when it returns a string or a struct: if completing it throws,
        // the block (which nothing owns yet) is given back before the exception goes on.
        template <typename T>
        [[nodiscard]] static CallResult<T> CompleteReturningSlot(const BridgeFunctions& bridge, bool returnsBlock,
                                                                 std::int32_t status, const char* operation,
                                                                 const ManagedDiagnosticBuffer& diagnostic,
                                                                 std::uint64_t slot)
        {
            try
            {
                return CompleteCall<T>(status, operation, diagnostic, slot);
            }
            catch (...)
            {
                if (returnsBlock && status == 0 && slot != 0)
                {
                    bridge.freeString(slot);
                }
                throw;
            }
        }
    };
} // namespace Iris::Detail
