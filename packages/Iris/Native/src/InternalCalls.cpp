#include "InternalCalls.hpp"

#include "Iris/InternalCall.hpp"
#include "LogSink.hpp"
#include "ManagedDiagnosticBuffer.hpp"
#include "Names.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace Iris::Detail
{
    namespace
    {
        struct Entry
        {
            std::string name;
            ValueKind returnKind;
            std::vector<std::int32_t> parameterKinds;
            void* function;
        };

        // Deliberately leaked, like the log sink: a registration must outlive static destruction because
        // managed code can still be calling the function.
        struct InternalCallState
        {
            std::mutex mutex;
            std::vector<Entry> entries;
            std::shared_ptr<const BridgeFunctions> bridge;
        };

        InternalCallState& State()
        {
            static InternalCallState* const state = new InternalCallState;
            return *state;
        }

        CallResult<void> Push(const BridgeFunctions& bridge, const Entry& entry)
        {
            ManagedDiagnosticBuffer diagnostic;
            const auto status = bridge.registerInternalCall(
                reinterpret_cast<const std::uint8_t*>(entry.name.data()), static_cast<std::int32_t>(entry.name.size()),
                static_cast<std::int32_t>(entry.returnKind), entry.parameterKinds.data(),
                static_cast<std::int32_t>(entry.parameterKinds.size()), entry.function, diagnostic.bytes.data(),
                diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
            return CompleteCall<void>(status, "RegisterInternalCall", diagnostic);
        }
    } // namespace

    CallResult<void> RegisterInternalCallRaw(std::string_view name, ValueKind returnKind,
                                             std::span<const ValueKind> parameterKinds, void* function)
    {
        if (const auto problem = NameProblem(name, "internal call"))
        {
            return Failure<void>(CallStatus::InvalidArgument, *problem);
        }
        if (parameterKinds.size() > 255)
        {
            return Failure<void>(CallStatus::InvalidArgument, "An internal call cannot have more than 255 parameters.");
        }
        Entry entry{std::string(name), returnKind, {}, function};
        for (const auto kind : parameterKinds)
        {
            entry.parameterKinds.push_back(static_cast<std::int32_t>(kind));
        }

        const std::lock_guard lock(State().mutex);
        auto& entries = State().entries;
        const auto existing = std::find_if(entries.begin(), entries.end(),
                                           [&](const Entry& candidate) { return candidate.name == entry.name; });
        if (existing != entries.end())
        {
            if (existing->function == entry.function && existing->returnKind == entry.returnKind &&
                existing->parameterKinds == entry.parameterKinds)
            {
                return Success<void>();
            }
            return Failure<void>(CallStatus::InvalidArgument,
                                 "The internal call '" + entry.name +
                                     "' is already registered with a different function or signature.");
        }
        if (State().bridge != nullptr)
        {
            auto pushed = Push(*State().bridge, entry);
            if (!pushed.Succeeded())
            {
                return pushed;
            }
        }
        entries.push_back(std::move(entry));
        return Success<void>();
    }

    CallResult<void> AttachInternalCalls(std::shared_ptr<const BridgeFunctions> bridge)
    {
        const std::lock_guard lock(State().mutex);
        for (const auto& entry : State().entries)
        {
            auto pushed = Push(*bridge, entry);
            if (!pushed.Succeeded())
            {
                return pushed;
            }
        }
        State().bridge = std::move(bridge);
        return Success<void>();
    }

    void ReportInternalCallException(const char* what) noexcept
    {
        try
        {
            std::string text = "A C++ exception escaped an internal call and was stopped at the boundary";
            if (what != nullptr)
            {
                text += ": ";
                text += what;
            }
            EmitLog(LogLevel::Error, text);
        }
        catch (...)
        {
        }
    }
} // namespace Iris::Detail
