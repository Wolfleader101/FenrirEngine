#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirTypes/Kind.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief One field of a script component or struct, as the script reported it: the generated table names its kind
     * and target, and measures its offset and size at run time.
     */
    struct ScriptFieldDescription
    {
        /** @brief The C# field name, which files spell. */
        std::string name;
        /** @brief Its kind: a plain kind, `struct`, `array`, `pointer`, `reference`, `entity` or `fixed-string`. */
        Kind kind = Kind::I32;
        /**
         * @brief For `struct` (or an `array` of structs): the full name of the struct, a script struct the report holds
         * or a type the registry has; for `reference` (or an `array` of them): the canonical name of the target type.
         */
        std::string target;
        /** @brief For `array`: the kind of each element. */
        Kind elementKind = Kind::I32;
        /** @brief Bytes from the start of the struct. */
        std::size_t offset = 0;
        /** @brief Bytes the field takes, measured by C#: checked against what its kind takes. */
        std::size_t size = 0;
        /** @brief For `array`: the element count; for `fixed-string`: the buffer's length in bytes. */
        std::size_t count = 0;
        /** @brief `[Transient]`: kept in the layout, never written to a file. */
        bool transient = false;
    };

    /**
     * @brief What a script assembly reports about one component type, or one struct a component holds, that it
     * defines.
     */
    struct ScriptComponentDescription
    {
        std::string name; // The type's full name.
        std::size_t size = 0;
        std::size_t alignment = 0;
        /** @brief Its public fields and fixed buffers, in declaration order; empty when the script reported none. */
        std::vector<ScriptFieldDescription> fields;
        /** @brief The bytes of `new T()`, `size` of them; empty for zero bytes. */
        std::vector<std::byte> defaults;
        /** @brief `[Transient]` on the type: never written to a file. */
        bool transient = false;
        /** @brief `[EditorOnly]` on the type: authoring data for an editor only. */
        bool editorOnly = false;
    };

    // An event type a script assembly reported while it loaded: a name, a size and an alignment, as for a component. An
    // event reports no fields, defaults or flags; only components are described to the type registry.
    using ScriptEventDescription = ScriptComponentDescription;

    // The script report: what a script assembly says it defines when it loads, the C# components and events, each with
    // its size and alignment, in the order it reports them, the structs its components hold by value, and its data
    // asset types. The engine checks it before it keeps the assembly.
    struct ScriptReport
    {
        std::vector<ScriptComponentDescription> components;
        std::vector<ScriptEventDescription> events;
        /** @brief The script-defined structs the components' fields hold, each with its fields, reported once. */
        std::vector<ScriptComponentDescription> structs;
        /**
         * @brief Each C# data asset type's description entry, a tree in the tree encoding (ADR-0083), bases before the
         * types extending them: `{name, version, formerly, base, transient, editorOnly, fields, structs, enums,
         * upgraders, default}`, as `DataAssetCodec.Entry` writes it. Admission reads and checks them.
         */
        std::vector<std::vector<std::byte>> dataAssets;
    };

    // The ECS's description of the component type a script reported.
    [[nodiscard]] inline DynamicComponentType ToDynamicType(const ScriptComponentDescription& description)
    {
        return DynamicComponentType(description.name, description.size, description.alignment);
    }

    /** @brief The kind the API description spells `name` (`i32`, `fixed-string`); empty when it spells none. */
    [[nodiscard]] inline std::optional<Kind> KindNamed(std::string_view name) noexcept
    {
        for (auto value = static_cast<int>(Kind::I8); value <= static_cast<int>(Kind::Path); ++value)
        {
            if (KindName(static_cast<Kind>(value)) == name)
            {
                return static_cast<Kind>(value);
            }
        }
        return std::nullopt;
    }
} // namespace Fenrir::Detail
