#include "Iris/Type.hpp"

#include "ManagedDiagnosticBuffer.hpp"
#include "TypeState.hpp"
#include "Utf8Buffer.hpp"

#include <utility>

namespace Iris
{
    Type::Type(std::shared_ptr<const State> typeState) noexcept : state(std::move(typeState))
    {
    }

    CallResult<TypeInfo> Type::GetInfo() const
    {
        const auto read = [this](std::int32_t field) {
            return Detail::ReadUtf8Buffer(
                "CopyTypeNameUtf8", [this, field](std::uint8_t* output, std::int32_t capacity, std::int32_t* required,
                                                  std::uint8_t* diagnostic, std::int32_t diagnosticCapacity,
                                                  std::int32_t* written, std::int32_t* truncated) {
                    return state->bridge->copyTypeName(state->component, state->type, field, output, capacity, required,
                                                       diagnostic, diagnosticCapacity, written, truncated);
                });
        };
        auto fullName = read(0);
        if (!fullName.Succeeded())
        {
            return Detail::PropagateFailure<TypeInfo>(fullName);
        }
        auto assemblyName = read(1);
        if (!assemblyName.Succeeded())
        {
            return Detail::PropagateFailure<TypeInfo>(assemblyName);
        }
        return Detail::Success<TypeInfo>(TypeInfo{std::move(*fullName.value), std::move(*assemblyName.value)});
    }
} // namespace Iris
