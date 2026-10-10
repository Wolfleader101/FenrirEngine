#include "Iris/Type.hpp"

#include "ManagedDiagnosticBuffer.hpp"
#include "MethodState.hpp"
#include "Names.hpp"
#include "TypeState.hpp"
#include "Utf8Buffer.hpp"

#include <utility>
#include <vector>

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

    CallResult<Detail::RawMethod> Type::FindMethodRaw(std::string_view name, Detail::ValueKind returnKind,
                                                      std::span<const Detail::ValueKind> parameterKinds) const
    {
        if (const auto problem = Detail::NameProblem(name, "method"))
        {
            return Detail::Failure<Detail::RawMethod>(CallStatus::InvalidArgument, *problem);
        }
        std::vector<std::int32_t> kinds;
        kinds.reserve(parameterKinds.size());
        for (const auto kind : parameterKinds)
        {
            kinds.push_back(static_cast<std::int32_t>(kind));
        }
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::uint64_t methodId = 0;
        const auto status = state->bridge->findMethod(
            state->component, state->type, reinterpret_cast<const std::uint8_t*>(name.data()),
            static_cast<std::int32_t>(name.size()), static_cast<std::int32_t>(returnKind), kinds.data(),
            static_cast<std::int32_t>(kinds.size()), &methodId, diagnostic.bytes.data(), diagnostic.Capacity,
            &diagnostic.written, &diagnostic.truncated);
        if (static_cast<CallStatus>(status) == CallStatus::Success && methodId == 0)
        {
            return Detail::Failure<Detail::RawMethod>(CallStatus::InvalidResponse,
                                                      "FindMethod returned the invalid handle 0.");
        }
        auto handle = std::make_shared<Detail::RawMethod::State>();
        handle->bridge = state->bridge;
        handle->component = state->component;
        handle->method = methodId;
        return Detail::CompleteCall<Detail::RawMethod>(status, "FindMethod", diagnostic,
                                                       Detail::RawMethod(std::move(handle)));
    }
} // namespace Iris
