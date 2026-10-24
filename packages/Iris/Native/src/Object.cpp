#include "Iris/Object.hpp"

#include "ManagedDiagnosticBuffer.hpp"
#include "ObjectState.hpp"

#include <utility>

namespace Iris
{
    Object::State::~State() noexcept
    {
        // Best effort. The bridge reports InvalidHandle if the component was released first.
        std::int32_t written = 0;
        std::int32_t truncated = 0;
        if (object != 0)
        {
            bridge->releaseObject(component, object, nullptr, 0, &written, &truncated);
        }
    }

    Object::Object(std::unique_ptr<State> objectState) noexcept : state(std::move(objectState))
    {
    }
    Object::~Object() noexcept = default;
    Object::Object(Object&&) noexcept = default;
    Object& Object::operator=(Object&&) noexcept = default;

    bool Object::IsValid() const noexcept
    {
        return state != nullptr && state->object != 0;
    }

    CallResult<void> Object::Release()
    {
        if (!IsValid())
        {
            return Detail::Success<void>();
        }
        // Invalidate this owner first so its destructor cannot release the object a second time.
        const auto object = std::exchange(state->object, 0);
        Detail::ManagedDiagnosticBuffer diagnostic;
        const auto status =
            state->bridge->releaseObject(state->component, object, diagnostic.bytes.data(), diagnostic.Capacity,
                                         &diagnostic.written, &diagnostic.truncated);
        return Detail::CompleteCall<void>(status, "ReleaseObject", diagnostic);
    }
} // namespace Iris
