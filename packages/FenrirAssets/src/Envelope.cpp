#include "Envelope.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/ReadTree.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        namespace Ser = Serialisation;

        // How a diagnostic names what it found: the node's kind, with a string's or number's text.
        std::string Found(const Ser::Value* node)
        {
            if (node == nullptr)
            {
                return "none";
            }
            switch (node->Kind())
            {
            case Ser::ValueKind::Object:
                return "an object";
            case Ser::ValueKind::Array:
                return "an array";
            case Ser::ValueKind::String:
                return std::format("\"{}\"", node->AsString());
            case Ser::ValueKind::Number: {
                const Ser::Number& number = node->AsNumber();
                switch (number.Width())
                {
                case Ser::NumberWidth::Signed:
                    return std::to_string(number.AsSigned());
                case Ser::NumberWidth::Unsigned:
                    return std::to_string(number.AsUnsigned());
                default:
                    return "a fractional number";
                }
            }
            case Ser::ValueKind::Bool:
                return node->AsBool() ? "true" : "false";
            case Ser::ValueKind::Null:
                return "null";
            }
            return "a value";
        }

        // A whole number from 1 up to `max`, as text reads it signed and code may build it unsigned; none for any other
        // node.
        std::optional<std::uint64_t> PositiveWhole(const Ser::Value* node, std::uint64_t max)
        {
            if (node == nullptr || node->Kind() != Ser::ValueKind::Number)
            {
                return std::nullopt;
            }
            const Ser::Number& number = node->AsNumber();
            if (number.Width() == Ser::NumberWidth::Signed && number.AsSigned() >= 1 &&
                static_cast<std::uint64_t>(number.AsSigned()) <= max)
            {
                return static_cast<std::uint64_t>(number.AsSigned());
            }
            if (number.Width() == Ser::NumberWidth::Unsigned && number.AsUnsigned() >= 1 && number.AsUnsigned() <= max)
            {
                return number.AsUnsigned();
            }
            return std::nullopt;
        }

        // A located diagnostic of the reader's own, at `node`, or at `fallback` when the node is missing or built in
        // code.
        Ser::Diagnostic LocatedAt(Ser::DiagnosticSeverity severity, std::string_view path, const Ser::Value* node,
                                  const Ser::Value& fallback, std::string message)
        {
            const Ser::Value& at = node != nullptr && node->location ? *node : fallback;
            Ser::Diagnostic diagnostic{.severity = severity, .path = std::string(path), .message = std::move(message)};
            if (at.location)
            {
                diagnostic.line = at.location->line;
                diagnostic.column = at.location->column;
            }
            return diagnostic;
        }

        void Append(std::vector<Ser::Diagnostic>& to, std::vector<Ser::Diagnostic>&& from)
        {
            to.insert(to.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
        }

        // The string node under `node` whose text is the one at `text`: the bridge hands a resolver the node's own
        // text, so its address finds the node, and with it the line and column. Null when none is.
        const Ser::Value* StringAt(const Ser::Value& node, const char* text)
        {
            switch (node.Kind())
            {
            case Ser::ValueKind::String:
                return node.AsString().data() == text ? &node : nullptr;
            case Ser::ValueKind::Object:
                for (const Ser::Member& member : node.AsObject().members)
                {
                    if (const Ser::Value* found = StringAt(member.value, text))
                    {
                        return found;
                    }
                }
                return nullptr;
            case Ser::ValueKind::Array:
                for (const Ser::Element& element : node.AsArray().elements)
                {
                    if (const Ser::Value* found = StringAt(element.value, text))
                    {
                        return found;
                    }
                }
                return nullptr;
            default:
                return nullptr;
            }
        }

        // A reference the file names that is left empty, refused by the path rules or naming no fitting target: where
        // its text is, so its error is located at its string, and the error's message.
        struct EmptiedReference
        {
            const char* text = nullptr;
            std::string message;
        };

        // How a diagnostic about the entry `name` starts: the sub-asset's name, or nothing for the file's top level.
        std::string EntryPrefix(std::string_view name)
        {
            return name.empty() ? std::string() : std::format("sub-asset `{}`: ", name);
        }

        // Whether `part`, one name of a path, is a drive (`C:`), as a Windows path's first is.
        bool IsDrive(std::string_view part)
        {
            return part.size() == 2 && part[1] == ':' && std::isalpha(static_cast<unsigned char>(part[0])) != 0;
        }
    } // namespace

    std::optional<std::string> FormatProblem(const Ser::Value* format)
    {
        if (PositiveWhole(format, EnvelopeFormat) == EnvelopeFormat)
        {
            return std::nullopt;
        }
        return std::format("expected `format` {}, found {}", EnvelopeFormat, Found(format));
    }

    std::function<std::string(ReferenceValue, const FieldInfo&)> CheckedReferenceWriter(
        const Ser::Resolvers& inner, const std::string& where, std::string_view path, ILogger& logger,
        std::vector<Ser::Diagnostic>& diagnostics, std::size_t& written)
    {
        return [&inner, &where, path, &logger, &diagnostics, &written](ReferenceValue reference,
                                                                       const FieldInfo& field) -> std::string {
            std::string named = inner.writeReference ? inner.writeReference(reference, field) : std::string();
            std::optional<std::string> problem;
            if (named.empty())
            {
                problem = std::format("{}`{}` names an asset with no asset path (one added with no name, or freed); it "
                                      "is written null",
                                      where, field.name);
            }
            else if (std::optional<std::string> refused = FilePathProblem(named))
            {
                problem = std::format("{}`{}` names \"{}\", which {}; it is written null", where, field.name, named,
                                      *refused);
            }
            if (!problem)
            {
                return named;
            }
            Ser::Diagnostic warning{.severity = Ser::DiagnosticSeverity::Warning,
                                    .path = std::string(path),
                                    .message = std::move(*problem)};
            logger.Warn("{}", Ser::FormatDiagnostic(warning));
            diagnostics.push_back(std::move(warning));
            ++written;
            return {};
        };
    }

    std::optional<std::string> FilePathProblem(std::string_view path)
    {
        if (path.starts_with(BuiltinPrefix))
        {
            return std::nullopt;
        }
        // The label is after the last `#`, as Load splits a path.
        path = path.substr(0, path.rfind('#'));
        constexpr std::string_view absolute = "is absolute: a path in a file is relative to the asset root";
        // Either slash counts, so a path written on Windows is checked as it would be read there.
        if (path.starts_with('/') || path.starts_with('\\'))
        {
            return std::string(absolute);
        }
        for (std::size_t start = 0; start <= path.size();)
        {
            const std::size_t end = std::min(path.find_first_of("/\\", start), path.size());
            const std::string_view part = path.substr(start, end - start);
            if (start == 0 && IsDrive(part))
            {
                return std::string(absolute);
            }
            if (part == "..")
            {
                return "climbs through `..`: a path in a file stays under the asset root";
            }
            start = end + 1;
        }
        return std::nullopt;
    }

    namespace
    {
        // What every step of one envelope's read shares: the file's path its diagnostics name, where types are found,
        // the store's part in the read, the log, and the file's diagnostics so far.
        struct EnvelopeReader
        {
            std::string_view path;
            const TypeRegistry& registry;
            const EnvelopeHooks& hooks;
            ILogger& logger;
            std::vector<Ser::Diagnostic>& diagnostics;

            // Logs `error`, one of the reader's own that fails no read, and records it with the file's diagnostics.
            void Report(Ser::Diagnostic error) const
            {
                logger.Error("{}", Ser::FormatDiagnostic(error));
                diagnostics.push_back(std::move(error));
            }
        };

        // What checking one entry of an envelope gave: the top level's, or one of its `sub`'s. Either the type,
        // version and data it is read from, or why it is not read.
        struct EntryCheck
        {
            std::string name;
            const TypeDescription* type = nullptr;
            std::uint32_t version = 0;
            Ser::Value* data = nullptr;
            std::optional<LoadFailure> failure;
            // The located diagnostic that failed the entry, also in the read's diagnostics.
            std::optional<Ser::Diagnostic> failureDiagnostic;
        };

        // What filling one checked entry gave.
        struct EntryRead
        {
            AssetBox value{nullptr, nullptr};
            bool incomplete = false;
            std::vector<std::pair<AssetKind, AssetSlot>> references;
        };

        // Checks one entry, the sub-asset `name` or the top level when it is empty: its `type`, `version` and `data`.
        // With `expected`, the type must be it or extend it; without, it must be a data asset type. A sub-asset
        // holding a `sub` of its own is refused. The diagnostic that fails the entry goes into the reader's, not logged
        // here.
        EntryCheck CheckEntry(const EnvelopeReader& reader, Ser::Object& entry, const Ser::Value& at,
                              std::string_view name, const TypeDescription* expected)
        {
            const std::string where = EntryPrefix(name);
            EntryCheck read{.name = std::string(name)};
            const auto fail = [&](LoadFailure reason, const Ser::Value* node, std::string message) -> EntryCheck {
                Ser::Diagnostic diagnostic = LocatedAt(Ser::DiagnosticSeverity::Error, reader.path, node, at,
                                                       std::format("{}{}", where, message));
                reader.diagnostics.push_back(diagnostic);
                read.failure = reason;
                read.failureDiagnostic = std::move(diagnostic);
                return std::move(read);
            };

            const Ser::Value* typeName = entry.Find("type");
            if (typeName == nullptr || typeName->Kind() != Ser::ValueKind::String)
            {
                return fail(LoadFailure::ImportFailed, typeName,
                            std::format("expected `type`, a type's name, found {}", Found(typeName)));
            }
            const TypeDescription* type = reader.registry.Find(typeName->AsString());
            const std::string wanted = expected != nullptr ? std::format("`{}` or a type extending it", expected->name)
                                                           : std::string("a data asset type");
            if (type == nullptr)
            {
                return fail(
                    LoadFailure::WrongType, typeName,
                    std::format("expected {}, found `{}`, which no package registers", wanted, typeName->AsString()));
            }
            if (expected != nullptr ? !reader.registry.IsA(type->name, expected->name) : type->role != Role::DataAsset)
            {
                return fail(LoadFailure::WrongType, typeName,
                            std::format("expected {}, found `{}`", wanted, type->name));
            }

            const Ser::Value* version = entry.Find("version");
            const std::optional<std::uint64_t> fromVersion =
                PositiveWhole(version, std::numeric_limits<std::uint32_t>::max());
            if (!fromVersion)
            {
                return fail(LoadFailure::ImportFailed, version,
                            std::format("expected `version`, a whole number from 1, found {}", Found(version)));
            }

            Ser::Value* data = entry.Find("data");
            if (data == nullptr)
            {
                return fail(LoadFailure::ImportFailed, data, "expected `data`, the asset's fields, found none");
            }

            // Sub-assets have none of their own (ADR-0076): a file's top level holds them all.
            if (const Ser::Value* nested = entry.Find("sub"); !name.empty() && nested != nullptr)
            {
                return fail(LoadFailure::ImportFailed, nested,
                            "expected no `sub`, found sub-assets of a sub-asset, which only a file's top level holds; "
                            "it is not loaded");
            }
            read.type = type;
            read.version = static_cast<std::uint32_t>(*fromVersion);
            read.data = data;
            return read;
        }

        // Fills a checked entry: Upgrades its data from its version, then reads it into an instance of its type, its
        // references named through the store's hook. Every diagnostic goes into the reader's, each logged once.
        EntryRead FillEntry(const EnvelopeReader& reader, const EntryCheck& entry)
        {
            const std::string where = EntryPrefix(entry.name);
            Ser::Value& data = *entry.data;
            Ser::UpgradeResult upgraded =
                Ser::Upgrade(reader.registry, entry.type->name, entry.version, data, reader.path, reader.logger);
            Append(reader.diagnostics, std::move(upgraded.diagnostics));

            // Every path the entry names passes the path rules before the store sees it (ADR-0076): a refused one is
            // left empty with the reader's own located error, and with no store to ask a path is unresolved, as the
            // bridge counts one. A target the store finds none for, or one of the wrong kind or type, is left empty
            // with one located error saying what was expected and what was found. No entity callbacks are given: a
            // data asset holds no entity (ADR-0075).
            std::vector<EmptiedReference> emptied;
            std::vector<std::pair<AssetKind, AssetSlot>> named;
            bool unresolved = false;
            Ser::Resolvers checked;
            checked.readReference = [&](std::string_view reference,
                                        const FieldInfo& field) -> std::optional<ReferenceValue> {
                if (std::optional<std::string> problem = FilePathProblem(reference))
                {
                    emptied.push_back(
                        {reference.data(),
                         std::format("{}expected `{}` to name an asset path under the asset root, found \"{}\", which "
                                     "{}; it is left empty",
                                     where, field.name, reference, *problem)});
                    return ReferenceValue{};
                }
                if (!reader.hooks.readReference)
                {
                    unresolved = true;
                    return ReferenceValue{};
                }
                ReferenceRead target = reader.hooks.readReference(reference, field);
                if (!target.problem.empty())
                {
                    emptied.push_back({reference.data(), std::format("{}`{}` names \"{}\": {}; it is left empty", where,
                                                                     field.name, reference, target.problem)});
                    return ReferenceValue{};
                }
                named.emplace_back(target.kind, AssetSlot{target.value.index, target.value.generation});
                return target.value;
            };

            auto instance = std::make_unique<DataValue>(*entry.type);
            Ser::BridgeResult bridged =
                Ser::FromTree(data, *entry.type, reader.registry, checked, instance->value, reader.path, reader.logger);
            Append(reader.diagnostics, std::move(bridged.diagnostics));
            for (EmptiedReference& reference : emptied)
            {
                reader.Report(LocatedAt(Ser::DiagnosticSeverity::Error, reader.path, StringAt(data, reference.text),
                                        data, std::move(reference.message)));
            }
            return {AssetBox(instance.release(), &DestroyAssetData<DataValue>),
                    bridged.status == Ser::BridgeStatus::Partial || unresolved, std::move(named)};
        }

        // Why `name` cannot name a sub-asset, or none: Load splits a path at its last `#`, so no path names a
        // sub-asset whose name holds one, and `file#` names none.
        std::optional<std::string> SubNameProblem(std::string_view name)
        {
            if (name.empty())
            {
                return "expected a sub-asset's name, found an empty one";
            }
            if (name.find('#') != std::string_view::npos)
            {
                return std::format("expected a sub-asset's name without `#`, found \"{}\"", name);
            }
            return std::nullopt;
        }
    } // namespace

    EnvelopeRead ReadDataEnvelope(std::span<const std::byte> bytes, std::string_view path,
                                  std::optional<std::string_view> label, const TypeDescription& expected,
                                  const TypeRegistry& registry, const EnvelopeHooks& hooks, ILogger& logger)
    {
        EnvelopeRead read;
        const EnvelopeReader reader{path, registry, hooks, logger, read.diagnostics};
        // The read fails with `reason`; `diagnostic`, the reader's own, is the store's to log with the failure.
        const auto fail = [&](LoadFailure reason, Ser::Diagnostic diagnostic) -> EnvelopeRead {
            read.failure = reason;
            read.failureMessage = Ser::FormatDiagnostic(diagnostic);
            read.diagnostics.push_back(std::move(diagnostic));
            read.entries.clear();
            return std::move(read);
        };

        Ser::ReadTreeResult tree = Ser::ReadTree(bytes, path, logger);
        Append(read.diagnostics, std::move(tree.diagnostics));
        if (!tree.tree)
        {
            read.failure = LoadFailure::ImportFailed;
            read.failureMessage = "the file is neither JSON5 nor the tree encoding";
            return read;
        }
        const Ser::Value& root = *tree.tree;
        if (root.Kind() != Ser::ValueKind::Object)
        {
            return fail(LoadFailure::ImportFailed,
                        LocatedAt(Ser::DiagnosticSeverity::Error, path, &root, root,
                                  std::format("expected a data asset's envelope object, found {}", Found(&root))));
        }
        Ser::Object& envelope = tree.tree->AsObject();

        const Ser::Value* format = envelope.Find("format");
        if (std::optional<std::string> problem = FormatProblem(format))
        {
            return fail(LoadFailure::ImportFailed,
                        LocatedAt(Ser::DiagnosticSeverity::Error, path, format, root, std::move(*problem)));
        }

        // `sub` maps each sub-asset's name, unique within the file (the parser keeps a repeated key's first), to its
        // entry. One that is not an object holds no sub-assets, with an error.
        Ser::Value* sub = envelope.Find("sub");
        if (sub != nullptr && sub->Kind() != Ser::ValueKind::Object)
        {
            reader.Report(
                LocatedAt(Ser::DiagnosticSeverity::Error, path, sub, root,
                          std::format("expected `sub`, an object of named sub-assets, found {}", Found(sub))));
            sub = nullptr;
        }
        // A label no sub-asset may have (`file#` gives an empty one) names none, even when `sub` has a key so named:
        // that entry is refused below.
        if (label && (SubNameProblem(*label) || sub == nullptr || sub->AsObject().Find(*label) == nullptr))
        {
            read.failure = LoadFailure::LabelNotFound;
            read.failureMessage = std::format("the file has no sub-asset named '{}'", *label);
            return read;
        }

        // Every entry is checked before any is filled, and the store told of each that will read, so every slot the
        // file fills is reserved before any of its references load: one naming this file, or a sub-asset of it, gets
        // its handle at once, and cycles resolve (ADR-0076 §3).
        std::vector<EntryCheck> checked;
        std::optional<std::size_t> requested;
        // Checks one entry into `checked`, as the one asked for when `isRequested`: a failure there fails the read,
        // its diagnostic left for the store to log; any other entry's failure is logged here and leaves that entry
        // out. False when the read failed.
        const auto checkEntry = [&](Ser::Object& entry, const Ser::Value& at, std::string_view name, bool isRequested) {
            EntryCheck check = CheckEntry(reader, entry, at, name, isRequested ? &expected : nullptr);
            if (check.failure)
            {
                if (isRequested)
                {
                    read.failure = check.failure;
                    read.failureMessage = Ser::FormatDiagnostic(*check.failureDiagnostic);
                    return false;
                }
                logger.Error("{}", Ser::FormatDiagnostic(*check.failureDiagnostic));
                return true;
            }
            if (isRequested)
            {
                requested = checked.size();
            }
            checked.push_back(std::move(check));
            return true;
        };

        if (!checkEntry(envelope, root, {}, !label))
        {
            return read;
        }
        if (sub != nullptr)
        {
            for (Ser::Member& member : sub->AsObject().members)
            {
                const bool isRequested = label && member.key == *label;
                // The label asked for passed this check above, so a name refused here is never the one asked for.
                if (std::optional<std::string> problem = SubNameProblem(member.key))
                {
                    reader.Report(LocatedAt(Ser::DiagnosticSeverity::Error, path, &member.value, *sub,
                                            std::format("{}; it is not loaded", *problem)));
                    continue;
                }
                if (member.value.Kind() != Ser::ValueKind::Object)
                {
                    Ser::Diagnostic error = LocatedAt(
                        Ser::DiagnosticSeverity::Error, path, &member.value, *sub,
                        std::format("sub-asset `{}`: expected an object of `type`, `version` and `data`, found {}",
                                    member.key, Found(&member.value)));
                    if (isRequested)
                    {
                        return fail(LoadFailure::ImportFailed, std::move(error));
                    }
                    reader.Report(std::move(error));
                    continue;
                }
                if (!checkEntry(member.value.AsObject(), member.value, member.key, isRequested))
                {
                    return read;
                }
            }
        }

        // The entry asked for passed, so nothing fails the read from here.
        if (hooks.reading)
        {
            for (const EntryCheck& check : checked)
            {
                hooks.reading(check.name, *check.type);
            }
        }
        read.requested = requested.value_or(0);
        for (const EntryCheck& check : checked)
        {
            EntryRead filled = FillEntry(reader, check);
            read.incomplete = read.incomplete || filled.incomplete;
            read.entries.push_back({check.name, std::move(filled.value), check.type, std::move(filled.references)});
        }
        // Read last: every entry's `data` is upgraded in place by now, and nothing points into the tree any more.
        read.tree = std::move(tree.tree);
        return read;
    }

    namespace
    {
        // The member of `node`, an object, keyed `key`; null when it has none, or is no object.
        const Ser::Member* MemberOf(const Ser::Value* node, std::string_view key)
        {
            if (node == nullptr || node->Kind() != Ser::ValueKind::Object)
            {
                return nullptr;
            }
            const auto& members = node->AsObject().members;
            const auto found = std::ranges::find(members, key, &Ser::Member::key);
            return found != members.end() ? &*found : nullptr;
        }

        // Gives each member of `object` the comments of the member of its key in `prior`, and `object` the closing
        // comments of `prior`, as ToTree does under `data`: for the envelope's own keys, which no type describes.
        void TakeComments(Ser::Object& object, const Ser::Value* prior)
        {
            if (prior == nullptr || prior->Kind() != Ser::ValueKind::Object)
            {
                return;
            }
            for (Ser::Member& member : object.members)
            {
                if (const Ser::Member* match = MemberOf(prior, member.key))
                {
                    member.leadingComments = match->leadingComments;
                    member.trailingComment = match->trailingComment;
                }
            }
            object.closingComments = prior->AsObject().closingComments;
        }

        // One entry's `type`, `version` and `data` (after `format` for the top level): `version` is its type's own,
        // since its `data` was upgraded to it on read, and `data` takes its comments from `prior`'s.
        Ser::Object EntryObject(const DataValue& value, const Ser::Value* prior, bool top, const TypeRegistry& registry,
                                const Ser::Resolvers& resolvers)
        {
            Ser::Object entry;
            if (top)
            {
                entry.Add("format", EnvelopeFormat);
            }
            entry.Add("type", value.type->name);
            entry.Add("version", value.type->version);
            const Ser::Member* priorData = MemberOf(prior, "data");
            entry.Add("data", Ser::ToTree(value.value, *value.type, registry, resolvers,
                                          priorData != nullptr ? &priorData->value : nullptr));
            TakeComments(entry, prior);
            return entry;
        }
    } // namespace

    EnvelopeWrite WriteDataEnvelope(const DataValue* top, std::span<const EnvelopeSub> subs, const Ser::Value* prior,
                                    const TypeRegistry& registry, const Ser::Resolvers& resolvers,
                                    std::string_view path, ILogger& logger)
    {
        EnvelopeWrite write;
        // The entry being written, as a warning names it.
        std::string where;
        // A reference is written as the path the store names it by, unless it has none a file can hold: then it is
        // `null`, with one warning, and counted (ADR-0079).
        Ser::Resolvers counting = resolvers;
        counting.writeReference =
            CheckedReferenceWriter(resolvers, where, path, logger, write.diagnostics, write.emptyReferences);

        Ser::Object envelope;
        if (top != nullptr)
        {
            envelope = EntryObject(*top, prior, true, registry, counting);
        }
        else
        {
            // The top level the store holds none of is written as the file has it.
            for (const std::string_view key : {"format", "type", "version", "data"})
            {
                if (const Ser::Member* member = MemberOf(prior, key))
                {
                    envelope.members.push_back(*member);
                }
            }
            TakeComments(envelope, prior);
        }

        // `sub` keeps the file's order, so a save moves no sub-asset (ADR-0077 §3); one the store holds none of is
        // kept as the file has it, as a value of an unknown type is.
        const Ser::Member* priorSub = MemberOf(prior, "sub");
        const Ser::Value* priorSubs =
            priorSub != nullptr && priorSub->value.Kind() == Ser::ValueKind::Object ? &priorSub->value : nullptr;
        const auto live = [&](std::string_view label) -> const DataValue* {
            const auto found = std::ranges::find(subs, label, &EnvelopeSub::label);
            return found != subs.end() ? found->value : nullptr;
        };
        Ser::Object sub;
        if (priorSubs != nullptr)
        {
            for (const Ser::Member& member : priorSubs->AsObject().members)
            {
                if (const DataValue* value = live(member.key))
                {
                    where = EntryPrefix(member.key);
                    sub.Add(member.key, EntryObject(*value, &member.value, false, registry, counting));
                }
                else
                {
                    sub.members.push_back(member);
                }
            }
        }
        for (const EnvelopeSub& entry : subs)
        {
            if (MemberOf(priorSubs, entry.label) == nullptr)
            {
                where = EntryPrefix(entry.label);
                sub.Add(std::string(entry.label), EntryObject(*entry.value, nullptr, false, registry, counting));
            }
        }
        if (!sub.members.empty())
        {
            TakeComments(sub, priorSubs);
            envelope.Add("sub", std::move(sub));
            TakeComments(envelope, prior);
        }

        write.tree = Ser::Value(std::move(envelope));
        if (prior != nullptr)
        {
            write.tree.leadingComments = prior->leadingComments;
        }
        return write;
    }
} // namespace Fenrir::Detail
