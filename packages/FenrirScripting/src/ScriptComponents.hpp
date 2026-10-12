#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ByteStorage.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

namespace Fenrir::Detail
{
    // What a script assembly reports about one component type it defines.
    struct ScriptComponentDescription
    {
        std::string name; // The type's full name.
        std::size_t size = 0;
        std::size_t alignment = 0;
    };

    // The EnTT storage id of a script component's pool. Derived from the name, with a prefix that keeps it apart from the
    // ids EnTT derives from C++ types.
    [[nodiscard]] entt::id_type ScriptStorageId(std::string_view name);

    // Whether the description can be a script component's: a size that is a multiple of a power-of-two alignment, no larger
    // than ByteStorage::MaxElementSize.
    [[nodiscard]] bool IsPlausible(const ScriptComponentDescription& description) noexcept;

    // A registration whose operations act on a ByteStorage named after the component, created in each list the first time
    // it is used. Nothing is created here, so registering and then discarding it leaves no trace in any list.
    [[nodiscard]] ComponentRegistration MakeScriptComponent(const ScriptComponentDescription& description);
} // namespace Fenrir::Detail
