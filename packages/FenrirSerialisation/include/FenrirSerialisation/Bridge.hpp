#pragma once

#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Fenrir
{
    class ILogger;
}

namespace Fenrir::Serialisation
{
    /**
     * @brief How the bridge turns `reference` and `entity` fields into the strings a file holds and back (ADR-0076 §2,
     * ADR-0078 §3): the caller's, since the bridge never knows an asset path's meaning, a store or an id map.
     *
     * Each callback is handed the field, whose `type.target` names a reference's target type. For an element of an
     * array, list or optional, it is a copy of the field holding it whose `type` is the element's, so `type.target`
     * names the target there too; its name and offset are the field's. `null` in a file is none both ways: the bridge
     * writes the empty handle, the null entity and an empty string from a callback as `null`, and reads `null` as the
     * empty handle or the null entity, with no callback asked and no diagnostic. A missing callback
     * writes `null`, and on read leaves the field empty with one error, counted in `unresolved`, as a read callback
     * returning nothing does.
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
     * - a `reference` or an `entity` is the string `writeReference` or `writeEntity` gives it, or `null` when that is
     *   empty, the callback is missing or the value is the empty handle or the null entity;
     * - a `pointer` field is never written, since its target is never described and its bytes mean nothing in a
     *   file; a pointer element of an array, list or optional is `null`.
     *
     * An enum value no enumerator has is written as its number.
     *
     * @param registry The registry the value's types are registered in; not read yet, since the description reaches
     *        every nested type itself.
     * @param resolvers Asked once for each `reference` or `entity` value written, an element apiece in an array.
     * @param prior The tree the value was read from, whose comments a rewrite keeps; not read yet.
     */
    [[nodiscard]] Value ToTree(const void* value, const TypeDescription& type, const TypeRegistry& registry,
                               const Resolvers& resolvers, const Value* prior = nullptr);

    /** @brief How a `FromTree` read went. */
    enum class BridgeStatus
    {
        /** @brief Every value in the tree read, and no key was dropped. */
        Succeeded,
        /** @brief The value read as far as it could: at least one of `BridgeResult`'s counts is non-zero. */
        Partial,
        /** @brief The registry lacks the type, so nothing was read; the caller keeps the tree and writes it back. */
        UnknownType,
    };

    /** @brief What `FromTree` gives: the status, what it counted and its diagnostics (ADR-0077 §7, ADR-0079 §3). */
    struct BridgeResult
    {
        /** @brief `Partial` whenever a count is non-zero. */
        BridgeStatus status = BridgeStatus::Succeeded;
        /** @brief Values that could not be read, so took their defaults: each has one diagnostic. */
        std::size_t defaulted = 0;
        /** @brief Keys that matched no field and no former name, warned and dropped. */
        std::size_t unknownFields = 0;
        /** @brief `reference` and `entity` values left empty because no callback named their string: each has one
         * diagnostic. */
        std::size_t unresolved = 0;
        /** @brief Every diagnostic, in the order found, each already logged once. */
        std::vector<Diagnostic> diagnostics;
    };

    /**
     * @brief Fills the described value at `value`, of type `type`, from `tree`, field by field through its
     * description's visitor (ADR-0079). The read is tolerant: what reads is kept, and what does not takes its default.
     *
     * `value` must be an instance `type`'s `Construct` made, since its fields are the defaults a field absent from the
     * tree keeps. Each kind reads the form `ToTree` writes it in. Then:
     * - a value that cannot be read takes its default, with one located error, and is counted in `defaulted`. That is
     *   a value of the wrong kind, an integer out of its kind's range, a float in an integer field, a finite number
     *   beyond an `f32`'s range, an unknown enumerator, `null` in a field that does not admit it, and a fixed array
     *   or bound type of the wrong length;
     * - a list's elements are each read so, a bad element taking the element type's default;
     * - a key matching no field and no `@formerly` name is warned, dropped and counted in `unknownFields`;
     * - a key matching a field's `@formerly` name reads into that field; when the current name is there too, the
     *   current one is read and the former is dropped with a warning, which no count counts. Of several former names
     *   the first the field lists is read, the others dropped so. Every field's current name is claimed before any
     *   former name, so a field keeps its own key though another field once had that name;
     * - `null` in an `optional` reads as disengaged;
     * - a `fixed-string` longer than N-1 bytes is cut by `ValueRef::SetText` on a UTF-8 boundary, with one warning
     *   (ADR-0078 §6);
     * - a tagged union reads `kind` first and selects that alternative before filling it. A missing or unknown `kind`
     *   keeps the union at its default, alternative and fields, with one error counted in `defaulted`;
     * - a `reference` or `entity` reads `null` as the empty handle or the null entity, and a string as what
     *   `readReference` or `readEntity` names it; a string the callback names nothing for, or with no callback to
     *   ask, leaves the value empty with one error, counted in `unresolved`. Any other node takes its default, counted
     *   in `defaulted`;
     * - a `pointer` field is never read, and is left at its default.
     *
     * An enum read from a number takes that number, as `ToTree` writes a value no enumerator has. A diagnostic is at
     * the node it is about, or at line 1, column 1 for a node built in code.
     *
     * @param registry When it lacks `type`, by canonical or former name, the result is `UnknownType`: nothing is read
     *        or logged, and the caller keeps the tree to write back unchanged (ADR-0077 §6).
     * @param resolvers Asked once for each `reference` or `entity` value the tree holds as a string.
     * @param path The path diagnostics name the tree's file by, as `ReadText`'s does.
     */
    [[nodiscard]] BridgeResult FromTree(const Value& tree, const TypeDescription& type, const TypeRegistry& registry,
                                        const Resolvers& resolvers, void* value, std::string_view path,
                                        ILogger& logger);
} // namespace Fenrir::Serialisation
