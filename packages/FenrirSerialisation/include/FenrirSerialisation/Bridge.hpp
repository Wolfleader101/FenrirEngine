#pragma once

#include "FenrirSerialisation/Value.hpp"

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Fenrir::Serialisation
{
    /**
     * @brief How the bridge turns `reference` and `entity` fields into the strings a file holds and back (ADR-0076 §2,
     * ADR-0078 §3): the caller's, since the bridge never knows an asset path's meaning, a store or an id map.
     *
     * Each callback is handed the field, whose `type.target` names a reference's target type. A missing callback leaves
     * the field empty on read and writes `null`.
     */
    struct Resolvers
    {
        /** @brief The asset `path` names, for a `reference` field; empty when it names none. */
        std::function<std::optional<ReferenceValue>(std::string_view path, const FieldInfo& field)> readReference;
        /** @brief The path a file names `reference` by, for a `reference` field; empty for none, written `null`. */
        std::function<std::string(ReferenceValue reference, const FieldInfo& field)> writeReference;
        /** @brief The entity `id` names, for an `entity` field; empty when it names none. */
        std::function<std::optional<EntityValue>(std::string_view id, const FieldInfo& field)> readEntity;
        /** @brief The id a file names `entity` by, for an `entity` field; empty for none, written `null`. */
        std::function<std::string(EntityValue entity, const FieldInfo& field)> writeEntity;
    };

    /**
     * @brief The value tree of the described value at `value`, of type `type`, walked through its description's
     * visitor (ADR-0079).
     *
     * Fields are written in declaration order, and a field equal to its default is left out (ADR-0075 §7). A disengaged
     * `optional` is left out too, unless its default holds a value: then it is `null`, which reads as disengaged. A
     * field's default is the same field of an instance `type`'s `Construct` makes, visited beside the value; a nested
     * struct compares field by field. Each kind's form:
     * - a struct is an `Object`, a `list` or fixed array an `Array`, and an enum its enumerator's name;
     * - a `string`, `fixed-string` or `path` is a `String`, and a `bool` a `Bool`;
     * - a number is a `Number` of its own width, and a `char16` an unsigned one holding its code unit;
     * - a bound type is an `Array` of its components in the order its description visits them (`[0, 1.8, 0]`);
     * - a tagged union is an `Object` whose first member is `kind`, its tag's enumerator name, followed by the fields
     * of its live alternative alone;
     * - a `reference` or an `entity` is `null`, until the resolvers write it;
     * - a `pointer` field is never written, since its target is never described and its bytes mean nothing in a
     *   file; a pointer element of an array, list or optional is `null`.
     *
     * An enum value no enumerator has is written as its number.
     *
     * @param registry The registry the value's types are registered in; not read yet, since the description reaches
     *        every nested type itself.
     * @param resolvers Not called yet: `reference` and `entity` fields are written `null`.
     * @param prior The tree the value was read from, whose comments a rewrite keeps; not read yet.
     */
    [[nodiscard]] Value ToTree(const void* value, const TypeDescription& type, const TypeRegistry& registry,
                               const Resolvers& resolvers, const Value* prior = nullptr);
} // namespace Fenrir::Serialisation
