#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/ValueRef.hpp"

namespace Fenrir
{
    class ILogger;
    class TypeRegistry;
    struct FieldInfo;
    struct TypeDescription;
} // namespace Fenrir

namespace Fenrir::Detail
{
    struct DataValue;

    /** @brief One asset an envelope holds: its top level, or one of the sub-assets in its `sub`. */
    struct EnvelopeEntry
    {
        /** @brief The sub-asset's name, its label in an asset path; empty for the file's top level. */
        std::string label;
        /** @brief The instance, of `type`. */
        AssetBox value{nullptr, nullptr};
        /** @brief The entry's type: the type expected, or one extending it, for the entry asked for; any data asset
         * type for the others. */
        const TypeDescription* type = nullptr;
        /** @brief Each asset its references named, by kind and slot, in the order read: what its owners own too. */
        std::vector<std::pair<AssetKind, AssetSlot>> references;
    };

    /**
     * @brief What reading a data asset's envelope gave: each asset in it that read, or why the one asked for did not,
     * and the diagnostics of the whole file.
     */
    struct EnvelopeRead
    {
        /**
         * @brief Every entry that read, the top level first and then `sub`'s in the file's order; empty when the read
         * failed. An entry that did not read is left out, with its diagnostic.
         */
        std::vector<EnvelopeEntry> entries;
        /** @brief Which of `entries` is the one asked for, when the read did not fail. */
        std::size_t requested = 0;
        /** @brief Why the read failed; empty when it gave the entry asked for. */
        std::optional<LoadFailure> failure;
        /** @brief What failed it, for the store's own error: the located diagnostic, when one did. */
        std::string failureMessage;
        /**
         * @brief Every located diagnostic of the file, in the order found: ReadTree's, Upgrade's and FromTree's, each
         * already logged, and the reader's own. Of those, each is logged here but the one that failed the read: the
         * store logs that with the failure.
         */
        std::vector<Serialisation::Diagnostic> diagnostics;
        /** @brief Whether FromTree read any entry's fields in part, as a reference with no resolver makes it with no
         * diagnostic. */
        bool incomplete = false;
        /**
         * @brief The file's tree as read, each entry's `data` upgraded to its type's version, when the read did not
         * fail: what a save of the file takes its comments, and the entries the store holds none of, from.
         */
        std::optional<Serialisation::Value> tree;
    };

    /** @brief What a reference in a file names: the asset, or why it names none. */
    struct ReferenceRead
    {
        /** @brief The target's kind, which the store's table of its slot is. */
        AssetKind kind = AssetKind::Data;
        /** @brief The target's slot, or the empty handle when `problem` says why there is none. */
        ReferenceValue value;
        /**
         * @brief Empty when `value` names the target; otherwise what was expected and what was found instead, such as
         * "expected `AssetsTest::Sword` or a type extending it, found `AssetsTest::WeaponData`".
         */
        std::string problem;
    };

    /** @brief How the store takes part in one envelope's read. */
    struct EnvelopeHooks
    {
        /**
         * @brief Names the target of a reference the file holds, given its path as written (already past the path
         * rules) and its field, whose `type.target` names the target's type. A problem becomes one located error at
         * the path's string. None leaves every reference empty, counted as the bridge counts a missing resolver.
         */
        std::function<ReferenceRead(std::string_view path, const FieldInfo& field)> readReference;
        /**
         * @brief Told each entry that will read, by its label (empty for the top level) and its type, once every
         * entry is checked and before any entry's fields, so before any of its references resolve. A read told of an
         * entry never fails after.
         */
        std::function<void(std::string_view label, const TypeDescription& type)> reading;
    };

    /** @brief Moves every diagnostic of `from` onto the end of `to`, in order. */
    void Append(std::vector<Serialisation::Diagnostic>& to, std::vector<Serialisation::Diagnostic>&& from);

    /**
     * @brief Logs `diagnostic`, a reader's or writer's own, once (an error as an error, a warning as a warning), and
     * appends it to `diagnostics`.
     */
    void Record(Serialisation::Diagnostic diagnostic, ILogger& logger,
                std::vector<Serialisation::Diagnostic>& diagnostics);

    /** @brief How a diagnostic names what it found at `node`: its kind, with a string's or number's text; `none` for
     * null. */
    [[nodiscard]] std::string Found(const Serialisation::Value* node);

    /**
     * @brief A whole number from 1 up to `max` at `node`, as text reads it signed and code may build it unsigned; none
     * for any other node, and for a missing one.
     */
    [[nodiscard]] std::optional<std::uint64_t> PositiveWhole(const Serialisation::Value* node, std::uint64_t max);

    /**
     * @brief A located diagnostic of an envelope reader's own, at `node`, or at `fallback` when the node is missing or
     * built in code.
     */
    [[nodiscard]] Serialisation::Diagnostic LocatedAt(Serialisation::DiagnosticSeverity severity, std::string_view path,
                                                      const Serialisation::Value* node,
                                                      const Serialisation::Value& fallback, std::string message);

    /**
     * @brief A reference a file names that a read left empty, refused by the path rules or naming no fitting target:
     * the address of its text, so its error is located at its string, and the error's message.
     */
    struct EmptiedReference
    {
        const char* text = nullptr;
        std::string message;
    };

    /** @brief What one value's `readReference` (CheckedReferenceReader) found, for ReportEmptied after its read. */
    struct ReferenceReading
    {
        /** @brief Each reference left empty, with the error to report at its string. */
        std::vector<EmptiedReference> emptied;
        /** @brief Each asset a reference named, by kind and slot, in the order read. */
        std::vector<std::pair<AssetKind, AssetSlot>> named;
        /** @brief Whether a reference was left empty for want of a store to ask, which no diagnostic names. */
        bool unresolved = false;
    };

    /**
     * @brief The `readReference` an envelope reader reads a value's references through (ADR-0076): a path refused by
     * FilePathProblem is left empty, and so is one `read` names no target for (its problem said); each is recorded in
     * `reading.emptied` after `where`, the prefix naming the value. With no `read`, every path is left empty and
     * `reading.unresolved` set. `read`, `where` and `reading` must outlive it.
     */
    [[nodiscard]] std::function<std::optional<ReferenceValue>(std::string_view, const FieldInfo&)>
    CheckedReferenceReader(const std::function<ReferenceRead(std::string_view, const FieldInfo&)>& read,
                           const std::string& where, ReferenceReading& reading);

    /**
     * @brief Reports each of `reading.emptied`, a located error at its string under `data` (the tree the value was
     * read from), logged once and appended to `diagnostics`, and clears it: how many there were.
     */
    std::size_t ReportEmptied(ReferenceReading& reading, const Serialisation::Value& data, std::string_view path,
                              ILogger& logger, std::vector<Serialisation::Diagnostic>& diagnostics);

    /** @brief The envelope's file format version (ADR-0077 §2): the one this reader reads and the writers write. */
    inline constexpr std::uint64_t EnvelopeFormat = 1;

    /**
     * @brief Why `format`, the envelope's `format` member (null when it has none), is not EnvelopeFormat, as a located
     * error's message; none when it is: the check every envelope's reader makes first.
     */
    [[nodiscard]] std::optional<std::string> FormatProblem(const Serialisation::Value* format);

    /**
     * @brief The `type` a scene envelope names (ADR-0078). No package registers it: a scene's layout is the scene
     * writer's and reader's own, not a described type's.
     */
    inline constexpr std::string_view SceneTypeName = "Fenrir::Scene";

    /** @brief The scene layout's version, a scene envelope's `version`: ADR-0078's layout is 1. */
    inline constexpr std::uint32_t SceneVersion = 1;

    /**
     * @brief The described components a scene block spells apart from its `components` (ADR-0078 §4, ADR-0080):
     * `Relationship` as `parent` and the order, `EntityId` as `id`, and `Instance` as `instance`. The writer never
     * writes them as entries, and the reader ignores an entry naming one.
     */
    inline constexpr std::string_view SpelledApart[] = {"Fenrir::Relationship", "Fenrir::EntityId", "Fenrir::Instance"};

    /**
     * @brief Whether `entity` is a live entity of `entities`: false for an empty entity, a destroyed one, or one of
     * another scene. A merge's parent and a subtree's root must be.
     */
    [[nodiscard]] inline bool LiveIn(EntityList& entities, const Entity& entity)
    {
        return entity.IsValid() && entity == entities.GetEntity(entity.GetId());
    }

    /**
     * @brief Calls `visit` for each entity in the scope of the instance `root` tops (ADR-0080 §2): every entity below
     * it down to, and including, each nested instance's root, whose own entities are that instance's scope. The ids in
     * one scope are its source file's, so a slash path's segment names one of them. Depth-first, in sibling order.
     */
    void ForEachInInstance(const Entity& root, const std::function<void(const Entity&)>& visit);

    /** @brief The entity in the scope of the instance `root` tops whose `EntityId` is `id`, or the null entity. */
    [[nodiscard]] Entity FindInInstance(const Entity& root, EntityId id);

    /** @brief An `Instance` naming the asset path `source`, or none when it is too long for one to hold (255 bytes). */
    [[nodiscard]] std::optional<Instance> InstanceOf(std::string_view source);

    /** @brief Whether an ancestor of `entity` is an instance's root, so its id is scoped by that root. */
    [[nodiscard]] bool InsideInstance(const Entity& entity);

    /**
     * @brief The entity a slash path names below `top`, an instance's root (`"<source id>/<source id>"`, the path's
     * segments after the instance's own): each segment found in the scope of the instance before it. Null when a
     * segment does not parse, names nothing in its scope, or names an entity that is no instance's root but is not the
     * last.
     */
    [[nodiscard]] Entity FollowSlashPath(Entity top, std::string_view rest);

    /**
     * @brief A `writeReference` that writes what `inner` names a reference by, unless it names no path or one a file
     * cannot hold (FilePathProblem): then it writes `null`, with one warning naming the field after `where`, logged and
     * appended to `diagnostics` with `path` as its file, and counts it in `nulled`. The data asset and scene writers
     * write asset handle fields through it; every argument is read when it is called, so each must outlive it.
     */
    [[nodiscard]] std::function<std::string(ReferenceValue, const FieldInfo&)> CheckedReferenceWriter(
        const Serialisation::Resolvers& inner, const std::string& where, std::string_view path, ILogger& logger,
        std::vector<Serialisation::Diagnostic>& diagnostics, std::size_t& nulled);

    /**
     * @brief Records a reference written `null`: a warning of `message` naming the file `path`, logged once and
     * appended to `diagnostics`, and one more in `nulled`. Every writer's `null` for want of a target goes through it.
     */
    void WarnWrittenNull(std::string message, std::string_view path, ILogger& logger,
                         std::vector<Serialisation::Diagnostic>& diagnostics, std::size_t& nulled);

    /**
     * @brief Why `path`, an asset path written in a file, is refused, or none when it is not (ADR-0076 §1, ADR-0085
     * §1): it is absolute, or it climbs through `..`. Only the part before its last `#`, its label, is checked, as Load
     * splits it; a `builtin:` name and a bare `#label` pass.
     */
    [[nodiscard]] std::optional<std::string> FilePathProblem(std::string_view path);

    /**
     * @brief Reads the `.asset.json5` envelope in `bytes` (ADR-0077), in these steps: ReadTree the bytes; check
     * `format` is 1; find the top level's `type` in `registry`, by its canonical or a former name, and check its type
     * (below), its `version` and its `data`; check each entry of `sub` the same way, from its own `type`, `version` and
     * `data` (ADR-0076, ADR-0079), refusing a sub-asset holding a `sub` of its own. Then, once `hooks.reading` is told
     * of every entry that passed, each in turn: Upgrade its `data` from its `version`, and FromTree it into an
     * instance its type's description constructs, its references named through `hooks.readReference` and no entity
     * callbacks, since a data asset holds no entity.
     *
     * The entry asked for, the top level or the sub-asset `label`, must be of `expected`; the others may be of any data
     * asset type. When it does not read, or `sub` has no `label`, the read fails. Any other entry that does not read is
     * left out with a located error, and the rest still read. Private to FenrirAssets: the store's door to it is Load.
     *
     * @param path The path its diagnostics name the file by.
     * @param label The sub-asset asked for, by its name in `sub`; none for the file's top level.
     */
    [[nodiscard]] EnvelopeRead ReadDataEnvelope(std::span<const std::byte> bytes, std::string_view path,
                                                std::optional<std::string_view> label, const TypeDescription& expected,
                                                const TypeRegistry& registry, const EnvelopeHooks& hooks,
                                                ILogger& logger);

    /** @brief One sub-asset an envelope is written with: its name in `sub`, and its instance. */
    struct EnvelopeSub
    {
        std::string_view label;
        const DataValue* value = nullptr;
    };

    /** @brief What writing a data asset's envelope gave: its tree, and what it wrote empty. */
    struct EnvelopeWrite
    {
        /** @brief The envelope, for WriteText. */
        Serialisation::Value tree;
        /** @brief A warning for each reference written empty, each logged once, at line 1, column 1. */
        std::vector<Serialisation::Diagnostic> diagnostics;
        /** @brief How many references were written empty. */
        std::size_t emptyReferences = 0;
    };

    /**
     * @brief The `.asset.json5` envelope of a data asset and its sub-assets (ADR-0077): `format`, then the top level's
     * `type`, `version` (its type's own) and `data`, then `sub`, each `data` through ToTree in declaration order with
     * its defaults left out. With `prior`, the file's tree as last read or written, every node takes the comments of
     * the node at its key path there, and `sub` keeps `prior`'s order: a sub-asset `subs` lacks is written as `prior`
     * holds it (one freed, or one that did not read), and one `prior` lacks follows, in `subs`' order. A `top` of null
     * writes `prior`'s top level as it holds it; one of the two must be given.
     *
     * Each reference is written through `resolvers.writeReference`. One it names no path for, or a path a file cannot
     * hold (FilePathProblem), is written `null` with a warning naming its field, and counted.
     *
     * @param path The path the warnings name the file by.
     */
    [[nodiscard]] EnvelopeWrite WriteDataEnvelope(const DataValue* top, std::span<const EnvelopeSub> subs,
                                                  const Serialisation::Value* prior, const TypeRegistry& registry,
                                                  const Serialisation::Resolvers& resolvers, std::string_view path,
                                                  ILogger& logger);
} // namespace Fenrir::Detail
