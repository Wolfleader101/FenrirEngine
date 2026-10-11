#include "Iris/Type.hpp"

#include "ManagedDiagnosticBuffer.hpp"
#include "MemberState.hpp"
#include "MethodState.hpp"
#include "Names.hpp"
#include "ObjectState.hpp"
#include "SnapshotReader.hpp"
#include "StructRegistration.hpp"
#include "TypeState.hpp"
#include "Utf8Buffer.hpp"

#include <cstring>
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

    CallResult<Detail::RawMethod> Type::FindMethodRaw(std::string_view name, Detail::KindSpec returnSpec,
                                                      std::span<const Detail::KindSpec> parameterKinds,
                                                      bool isStatic) const
    {
        if (const auto problem = Detail::NameProblem(name, "method"))
        {
            return Detail::Failure<Detail::RawMethod>(CallStatus::InvalidArgument, *problem);
        }
        const auto returnKind = Detail::ResolveWireKind(*state->bridge, state->component, returnSpec);
        if (!returnKind.Succeeded())
        {
            return Detail::PropagateFailure<Detail::RawMethod>(returnKind);
        }
        std::vector<std::int32_t> kinds;
        kinds.reserve(parameterKinds.size());
        for (const auto& spec : parameterKinds)
        {
            const auto kind = Detail::ResolveWireKind(*state->bridge, state->component, spec);
            if (!kind.Succeeded())
            {
                return Detail::PropagateFailure<Detail::RawMethod>(kind);
            }
            kinds.push_back(*kind.value);
        }
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::uint64_t methodId = 0;
        const auto status = (isStatic ? state->bridge->findMethod : state->bridge->findInstanceMethod)(
            state->component, state->type, reinterpret_cast<const std::uint8_t*>(name.data()),
            static_cast<std::int32_t>(name.size()), *returnKind.value, kinds.data(),
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
        handle->returnsBlock =
            returnSpec.kind == Detail::ValueKind::String || returnSpec.kind == Detail::ValueKind::Struct;
        return Detail::CompleteCall<Detail::RawMethod>(status, "FindMethod", diagnostic,
                                                       Detail::RawMethod(std::move(handle)));
    }

    CallResult<Object> Type::CreateObjectRaw(std::span<const Detail::KindSpec> parameterKinds,
                                             std::span<std::uint64_t> slots,
                                             std::span<const Object* const> objects) const
    {
        if (const auto problem = Detail::RawMethod::ResolveObjects(state->component, slots, objects))
        {
            return Detail::Failure<Object>(problem->first, problem->second);
        }
        std::vector<std::int32_t> kinds;
        kinds.reserve(parameterKinds.size());
        for (const auto& spec : parameterKinds)
        {
            const auto kind = Detail::ResolveWireKind(*state->bridge, state->component, spec);
            if (!kind.Succeeded())
            {
                return Detail::PropagateFailure<Object>(kind);
            }
            kinds.push_back(*kind.value);
        }
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::uint64_t objectId = 0;
        const auto status = state->bridge->createObject(
            state->component, state->type, kinds.data(), slots.data(), static_cast<std::int32_t>(kinds.size()),
            &objectId, diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        if (static_cast<CallStatus>(status) == CallStatus::Success && objectId == 0)
        {
            return Detail::Failure<Object>(CallStatus::InvalidResponse, "CreateObject returned the invalid handle 0.");
        }
        // The owner exists before the result is inspected, so a handle that arrives with a malformed
        // response is still released.
        auto owned = std::make_unique<Object::State>();
        owned->bridge = state->bridge;
        owned->component = state->component;
        owned->object = objectId;
        return Detail::CompleteCall<Object>(status, "CreateObject", diagnostic, Object(std::move(owned)));
    }

    CallResult<Detail::RawMember> Type::FindMemberRaw(std::string_view name, bool isProperty,
                                                      Detail::KindSpec spec) const
    {
        if (const auto problem = Detail::NameProblem(name, "member"))
        {
            return Detail::Failure<Detail::RawMember>(CallStatus::InvalidArgument, *problem);
        }
        const auto wireKind = Detail::ResolveWireKind(*state->bridge, state->component, spec);
        if (!wireKind.Succeeded())
        {
            return Detail::PropagateFailure<Detail::RawMember>(wireKind);
        }
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::uint64_t memberId = 0;
        const auto status = state->bridge->findMember(
            state->component, state->type, reinterpret_cast<const std::uint8_t*>(name.data()),
            static_cast<std::int32_t>(name.size()), isProperty ? 1 : 0, *wireKind.value, &memberId,
            diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        if (static_cast<CallStatus>(status) == CallStatus::Success && memberId == 0)
        {
            return Detail::Failure<Detail::RawMember>(CallStatus::InvalidResponse,
                                                      "FindMember returned the invalid handle 0.");
        }
        auto handle = std::make_shared<Detail::RawMember::State>();
        handle->bridge = state->bridge;
        handle->component = state->component;
        handle->member = memberId;
        handle->returnsBlock = spec.kind == Detail::ValueKind::String || spec.kind == Detail::ValueKind::Struct;
        return Detail::CompleteCall<Detail::RawMember>(status, "FindMember", diagnostic,
                                                       Detail::RawMember(std::move(handle)));
    }

    namespace
    {
        const char* const Malformed = "The bridge returned a malformed type snapshot.";

        // Runs one snapshot query and hands the block's bytes to `parse`, which fills the result. The block is
        // given back to the bridge whatever happens.
        template <typename T, typename Parse>
        CallResult<T> Snapshot(const Detail::BridgeFunctions& bridge, std::uint64_t component, std::uint64_t type,
                               std::int32_t query, const char* operation, Parse parse)
        {
            Detail::ManagedDiagnosticBuffer diagnostic;
            std::uint64_t block = 0;
            const auto status = bridge.queryType(component, type, query, &block, diagnostic.bytes.data(),
                                                 diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
            try
            {
                if (static_cast<CallStatus>(status) != CallStatus::Success || block == 0)
                {
                    return Detail::CompleteCall<T>(status, operation, diagnostic, T{});
                }
                const auto* memory = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(block));
                std::int64_t length = 0;
                std::memcpy(&length, memory, sizeof(length));
                Detail::SnapshotReader reader(memory + sizeof(length), static_cast<std::size_t>(length));
                T value{};
                parse(reader, value);
                bridge.freeString(block);
                block = 0;
                if (!reader.Finished())
                {
                    return Detail::Failure<T>(CallStatus::InvalidResponse, Malformed);
                }
                return Detail::CompleteCall<T>(status, operation, diagnostic, std::move(value));
            }
            catch (...)
            {
                if (block != 0)
                {
                    bridge.freeString(block);
                }
                throw;
            }
        }

        TypeInfo ReadTypeInfo(Detail::SnapshotReader& reader)
        {
            TypeInfo info;
            info.fullName = reader.String();
            info.assemblyName = reader.String();
            return info;
        }
    } // namespace

    CallResult<std::optional<TypeInfo>> Type::GetBaseType() const
    {
        return Snapshot<std::optional<TypeInfo>>(*state->bridge, state->component, state->type, 0, "QueryType",
                                                 [](Detail::SnapshotReader& reader, std::optional<TypeInfo>& value) {
                                                     if (reader.Int32() == 1)
                                                     {
                                                         value = ReadTypeInfo(reader);
                                                     }
                                                 });
    }

    CallResult<std::vector<TypeInfo>> Type::GetInterfaces() const
    {
        return Snapshot<std::vector<TypeInfo>>(*state->bridge, state->component, state->type, 1, "QueryType",
                                               [](Detail::SnapshotReader& reader, std::vector<TypeInfo>& value) {
                                                   const std::int32_t count = reader.Int32();
                                                   for (std::int32_t index = 0; reader.Ok() && index < count; ++index)
                                                   {
                                                       value.push_back(ReadTypeInfo(reader));
                                                   }
                                               });
    }

    CallResult<std::vector<AttributeInfo>> Type::GetAttributes() const
    {
        return Snapshot<std::vector<AttributeInfo>>(
            *state->bridge, state->component, state->type, 2, "QueryType",
            [](Detail::SnapshotReader& reader, std::vector<AttributeInfo>& value) {
                const std::int32_t count = reader.Int32();
                for (std::int32_t index = 0; reader.Ok() && index < count; ++index)
                {
                    AttributeInfo attribute;
                    attribute.type = ReadTypeInfo(reader);
                    const std::int32_t arguments = reader.Int32();
                    for (std::int32_t position = 0; reader.Ok() && position < arguments; ++position)
                    {
                        AttributeArgument argument;
                        argument.name = reader.String();
                        argument.typeName = reader.String();
                        switch (reader.Int32())
                        {
                        case 1:
                            argument.value = reader.Int64() != 0;
                            break;
                        case 2:
                            argument.value = reader.Int64();
                            break;
                        case 3:
                            argument.value = static_cast<std::uint64_t>(reader.Int64());
                            break;
                        case 4:
                            argument.value = reader.Double();
                            break;
                        case 5:
                            argument.value = reader.String();
                            break;
                        default:
                            break;
                        }
                        attribute.arguments.push_back(std::move(argument));
                    }
                    value.push_back(std::move(attribute));
                }
            });
    }

    CallResult<bool> Type::IsAssignableTo(const Type& other) const
    {
        if (other.state->component != state->component)
        {
            return Detail::Failure<bool>(CallStatus::InvalidArgument, "The types belong to different components.");
        }
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::int32_t result = 0;
        const auto status = state->bridge->isTypeAssignable(state->component, state->type, other.state->type, nullptr,
                                                            0, &result, diagnostic.bytes.data(), diagnostic.Capacity,
                                                            &diagnostic.written, &diagnostic.truncated);
        return Detail::CompleteCall<bool>(status, "IsTypeAssignable", diagnostic, result != 0);
    }

    CallResult<bool> Type::IsAssignableTo(std::string_view fullName) const
    {
        if (const auto problem = Detail::NameProblem(fullName, "type"))
        {
            return Detail::Failure<bool>(CallStatus::InvalidArgument, *problem);
        }
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::int32_t result = 0;
        const auto status = state->bridge->isTypeAssignable(
            state->component, state->type, 0, reinterpret_cast<const std::uint8_t*>(fullName.data()),
            static_cast<std::int32_t>(fullName.size()), &result, diagnostic.bytes.data(), diagnostic.Capacity,
            &diagnostic.written, &diagnostic.truncated);
        return Detail::CompleteCall<bool>(status, "IsTypeAssignable", diagnostic, result != 0);
    }
} // namespace Iris
