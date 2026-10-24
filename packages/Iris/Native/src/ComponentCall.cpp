#include "ComponentCall.hpp"

#include "ObjectState.hpp"
#include "StringBlock.hpp"

#include <cstring>

namespace Iris
{
    std::optional<std::pair<CallStatus, std::string>> Detail::ComponentCall::ObjectProblem(
        const Object& object, std::uint64_t component, std::optional<std::size_t> argument)
    {
        const std::string subject =
            argument ? "Argument " + std::to_string(*argument) + " is an object" : std::string("The object");
        if (!object.IsValid())
        {
            return std::make_pair(CallStatus::InvalidHandle,
                                  subject + (argument ? " that has" : " has") + " been released or moved.");
        }
        if (object.state->component != component)
        {
            return std::make_pair(CallStatus::InvalidArgument, argument ? subject + " from another component."
                                                                        : subject + " belongs to another component.");
        }
        return std::nullopt;
    }

    std::optional<std::pair<CallStatus, std::string>> Detail::ComponentCall::ResolveObjects(
        std::uint64_t component, std::span<std::uint64_t> slots, std::span<const Object* const> objects)
    {
        for (std::size_t index = 0; index < objects.size() && index < slots.size(); ++index)
        {
            const Object* object = objects[index];
            if (object == nullptr)
            {
                continue;
            }
            if (auto problem = ObjectProblem(*object, component, index))
            {
                return problem;
            }
            slots[index] = object->state->object;
        }
        return std::nullopt;
    }

    std::optional<std::pair<CallStatus, std::string>> Detail::ComponentCall::TargetHandle(std::uint64_t component,
                                                                                          const Object* target,
                                                                                          std::uint64_t& handle)
    {
        handle = 0;
        if (target == nullptr)
        {
            return std::nullopt;
        }
        if (auto problem = ObjectProblem(*target, component, std::nullopt))
        {
            return problem;
        }
        handle = target->state->object;
        return std::nullopt;
    }

    Object Detail::CallOrigin::Adopt(std::uint64_t handle) const
    {
        auto owned = std::make_unique<Object::State>();
        owned->bridge = bridge;
        owned->component = component;
        owned->object = handle;
        return Object(std::move(owned));
    }

    std::optional<std::string> Detail::CallOrigin::TakeString(std::uint64_t block) const
    {
        return Detail::TakeString(*bridge, block);
    }

    void Detail::CallOrigin::TakeStruct(std::uint64_t block, void* out, std::size_t size) const noexcept
    {
        Detail::TakeBytes(*bridge, block, out, size);
    }
} // namespace Iris
