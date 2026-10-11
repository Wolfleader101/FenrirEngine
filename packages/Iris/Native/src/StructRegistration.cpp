#include "StructRegistration.hpp"

#include "ManagedDiagnosticBuffer.hpp"

#include <limits>
#include <string>

namespace Iris::Detail
{
    namespace
    {
        // The text the bridge parses: "TypeName|size", then "name|offset|kind|nestedTypeName" per field.
        std::string Describe(const StructInfo& info)
        {
            std::string text(info.typeName);
            text += '|' + std::to_string(info.size);
            for (const auto& field : info.fields)
            {
                text += '\n';
                text += field.name;
                text += '|' + std::to_string(field.offset) + '|' +
                        std::to_string(static_cast<std::int32_t>(field.kind)) + '|';
                if (field.nested != nullptr)
                {
                    text += field.nested->typeName;
                }
            }
            return text;
        }

        CallResult<std::uint64_t> Register(const BridgeFunctions& bridge, std::uint64_t component,
                                           const StructInfo& info)
        {
            for (const auto& field : info.fields)
            {
                if (field.nested != nullptr)
                {
                    auto nested = Register(bridge, component, *field.nested);
                    if (!nested.Succeeded())
                    {
                        return nested;
                    }
                }
            }
            const std::string text = Describe(info);
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            {
                return Failure<std::uint64_t>(CallStatus::InvalidArgument, "The struct mapping is too large.");
            }
            ManagedDiagnosticBuffer diagnostic;
            std::uint64_t id = 0;
            const auto status = bridge.registerStruct(
                component, reinterpret_cast<const std::uint8_t*>(text.data()), static_cast<std::int32_t>(text.size()),
                &id, diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
            if (static_cast<CallStatus>(status) == CallStatus::Success && (id == 0 || id >= (1U << 22)))
            {
                return Failure<std::uint64_t>(CallStatus::InvalidResponse,
                                              "RegisterStruct returned an invalid handle.");
            }
            return CompleteCall<std::uint64_t>(status, "RegisterStruct", diagnostic, id);
        }
    } // namespace

    CallResult<std::int32_t> ResolveWireKind(const BridgeFunctions& bridge, std::uint64_t component,
                                             const KindSpec& spec)
    {
        if (spec.kind != ValueKind::Struct)
        {
            return Success<std::int32_t>(static_cast<std::int32_t>(spec.kind));
        }
        auto id = Register(bridge, component, *spec.info);
        if (!id.Succeeded())
        {
            return PropagateFailure<std::int32_t>(id);
        }
        return Success<std::int32_t>(
            static_cast<std::int32_t>((*id.value << 8U) | static_cast<std::uint64_t>(ValueKind::Struct)));
    }
} // namespace Iris::Detail
