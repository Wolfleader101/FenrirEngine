// Assets::Move (ADR-0076 §5, ADR-0079 §6): moves one file under the asset root and rewrites every reference to it in
// the root's envelopes, editing each file's own tree so its comments and every other value stay as they were.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"
#include "Envelope.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

namespace Fenrir
{
    namespace
    {
        namespace Ser = Serialisation;
        using Detail::RootRelative;
        using Detail::Utf8Path;
        using Detail::Utf8Text;

        // ReadText logs each diagnostic; a move has no logger, and returns a file's syntax error in its result.
        class NullLogger final : public ILogger
        {
          public:
            void AddLogger(std::shared_ptr<ILogger>) override
            {
            }
            void RemoveLogger(std::shared_ptr<ILogger>) override
            {
            }

          protected:
            void LogImpl(std::string_view) override
            {
            }
            void InfoImpl(std::string_view) override
            {
            }
            void WarnImpl(std::string_view) override
            {
            }
            void ErrorImpl(std::string_view) override
            {
            }
            void FatalImpl(std::string_view) override
            {
            }
        };

        // Each field a visit pushes, in the order pushed: for a data asset, its base's fields first.
        struct VisitedField
        {
            const FieldInfo* info = nullptr;
            ValueRef value;
        };

        class FieldCollector final : public FieldVisitor
        {
          public:
            explicit FieldCollector(std::vector<VisitedField>& fields) noexcept : m_fields(fields)
            {
            }

            void Field(const FieldInfo& field, ValueRef value) override
            {
                m_fields.push_back({&field, value});
            }

          private:
            std::vector<VisitedField>& m_fields;
        };

        std::vector<VisitedField> FieldsOf(ValueRef value)
        {
            std::vector<VisitedField> fields;
            FieldCollector collector(fields);
            value.Visit(collector);
            return fields;
        }

        // Finds the reference strings in one entry's `data` tree and rewrites those naming the moved file. It walks the
        // tree as FromTree reads it, through a scratch instance of the entry's type, which gives a list its elements,
        // an optional its value and a tagged union its live alternative, so every field a reference can be reached
        // through is visited. Nothing is read into the instance.
        class ReferenceRewriter
        {
          public:
            ReferenceRewriter(std::string_view from, std::string_view to) noexcept : m_from(from), m_to(to)
            {
            }

            // The rewritten references of `data`, of `type`, in place; how many.
            std::size_t Rewrite(Ser::Value& data, const TypeDescription& type)
            {
                Detail::DataValue scratch(type);
                const ValueType valueType{.kind = Kind::Struct, .type = &type};
                m_count = 0;
                Walk(data, ValueRef(scratch.value, valueType));
                return m_count;
            }

          private:
            void Walk(Ser::Value& node, ValueRef target)
            {
                switch (target.GetKind())
                {
                case Kind::Reference:
                    RewriteReference(node);
                    break;
                case Kind::Struct:
                    WalkStruct(node, target);
                    break;
                case Kind::Array:
                    if (node.Kind() == Ser::ValueKind::Array && node.AsArray().elements.size() == target.Size())
                    {
                        WalkElements(node.AsArray(), target);
                    }
                    break;
                case Kind::List:
                    if (node.Kind() == Ser::ValueKind::Array)
                    {
                        target.Resize(node.AsArray().elements.size());
                        WalkElements(node.AsArray(), target);
                    }
                    break;
                case Kind::Optional:
                    if (node.Kind() != Ser::ValueKind::Null)
                    {
                        Walk(node, target.HasValue() ? target.Value() : target.Emplace());
                    }
                    break;
                default:
                    break;
                }
            }

            // `from` becomes `to`, and `from#name` becomes `to#name`; a bare `#name` names the file it is in, which
            // moves with it. The file part is compared as Load resolves it, so `./a/b.json5` and `a\b.json5` name
            // `a/b.json5` too, and are rewritten in the form `to` is given.
            void RewriteReference(Ser::Value& node)
            {
                if (node.Kind() != Ser::ValueKind::String)
                {
                    return;
                }
                const std::string& path = node.AsString();
                const std::size_t hash = std::min(path.rfind('#'), path.size());
                const std::string_view file = std::string_view(path).substr(0, hash);
                if (file.empty() || file.starts_with(Detail::BuiltinPrefix) || Detail::FilePathProblem(file))
                {
                    return;
                }
                std::string normal(file);
                std::ranges::replace(normal, '\\', '/');
                if (Utf8Text(Utf8Path(normal).lexically_normal()) == m_from)
                {
                    Replace(node, std::string(m_to) + path.substr(hash));
                }
            }

            void Replace(Ser::Value& node, std::string path)
            {
                // A node holds its location beside its value; the comments are on the member or element holding it.
                const std::optional<Ser::Location> location = node.location;
                node = Ser::Value(std::move(path));
                node.location = location;
                ++m_count;
            }

            void WalkElements(Ser::Array& array, ValueRef target)
            {
                for (std::size_t i = 0; i < array.elements.size(); ++i)
                {
                    Walk(array.elements[i].value, target.Element(i));
                }
            }

            void WalkStruct(Ser::Value& node, ValueRef target)
            {
                const TypeDescription* type = target.Description();
                if (type == nullptr)
                {
                    return;
                }
                if (type->role == Role::BoundType)
                {
                    // A bound type is an array of its components, in the order its description visits them.
                    const std::vector<VisitedField> components = FieldsOf(target);
                    if (node.Kind() == Ser::ValueKind::Array && node.AsArray().elements.size() == components.size())
                    {
                        for (std::size_t i = 0; i < components.size(); ++i)
                        {
                            Walk(node.AsArray().elements[i].value, components[i].value);
                        }
                    }
                    return;
                }
                if (node.Kind() != Ser::ValueKind::Object)
                {
                    return;
                }
                if (type->IsTaggedUnion())
                {
                    WalkUnion(node.AsObject(), target, *type);
                    return;
                }
                WalkFields(node.AsObject(), FieldsOf(target), {});
            }

            // `kind` selects the live alternative, whose fields sit beside it in the union's object.
            void WalkUnion(Ser::Object& object, ValueRef target, const TypeDescription& type)
            {
                const Ser::Value* kind = object.Find(type.tag.name);
                const TypeDescription* tagType = type.tag.type.type;
                if (kind == nullptr || kind->Kind() != Ser::ValueKind::String || tagType == nullptr)
                {
                    return;
                }
                const auto selected =
                    std::ranges::find(tagType->enumerators, std::string_view(kind->AsString()), &Enumerator::name);
                if (selected == tagType->enumerators.end() || !target.SelectAlternative(selected->value))
                {
                    return;
                }
                const std::vector<VisitedField> fields = FieldsOf(target);
                if (fields.size() < 2)
                {
                    return;
                }
                const VisitedField& live = fields[1];
                const TypeDescription* alternative = live.value.Description();
                if (live.info->type.kind == Kind::Struct && alternative != nullptr &&
                    alternative->role != Role::BoundType && !alternative->IsTaggedUnion())
                {
                    WalkFields(object, FieldsOf(live.value), type.tag.name);
                    return;
                }
                WalkFields(object, {live}, type.tag.name);
            }

            // Each field from the member of its name, or else of the first of its former names present, as FromTree
            // reads it. `tagKey` is a tagged union's `kind`, which no field reads.
            void WalkFields(Ser::Object& object, const std::vector<VisitedField>& fields, std::string_view tagKey)
            {
                std::vector<bool> used(object.members.size(), false);
                const auto claim = [&](std::string_view key) -> Ser::Member* {
                    for (std::size_t m = 0; m < object.members.size(); ++m)
                    {
                        if (!used[m] && object.members[m].key == key)
                        {
                            used[m] = true;
                            return &object.members[m];
                        }
                    }
                    return nullptr;
                };
                if (!tagKey.empty())
                {
                    claim(tagKey);
                }
                std::vector<Ser::Member*> sources(fields.size(), nullptr);
                for (std::size_t f = 0; f < fields.size(); ++f)
                {
                    sources[f] = claim(fields[f].info->name);
                }
                for (std::size_t f = 0; f < fields.size(); ++f)
                {
                    for (const std::string_view formerName : fields[f].info->formerly)
                    {
                        if (sources[f] != nullptr)
                        {
                            break;
                        }
                        sources[f] = claim(formerName);
                    }
                    if (sources[f] != nullptr)
                    {
                        Walk(sources[f]->value, fields[f].value);
                    }
                }
            }

            std::string_view m_from;
            std::string_view m_to;
            std::size_t m_count = 0;
        };

        // One entry of an envelope to scan: its `data`, and its type's description.
        struct ScanEntry
        {
            Ser::Value* data = nullptr;
            const TypeDescription* type = nullptr;
        };

        // The entries of the envelope `tree`, the top level and each of `sub`'s, or why it is not scanned.
        std::optional<std::string> EntriesOf(Ser::Value& tree, const TypeRegistry& registry,
                                             std::vector<ScanEntry>& entries)
        {
            if (tree.Kind() != Ser::ValueKind::Object)
            {
                return "it is no envelope: its root is not an object";
            }
            const auto entryOf = [&](Ser::Object& object, std::string_view where) -> std::optional<std::string> {
                const Ser::Value* type = object.Find("type");
                if (type == nullptr || type->Kind() != Ser::ValueKind::String)
                {
                    return std::format("it is no envelope: {} has no `type` string", where);
                }
                const TypeDescription* description = registry.Find(type->AsString());
                if (description == nullptr)
                {
                    return std::format("the type registry has no `{}`, the type of {}", type->AsString(), where);
                }
                if (Ser::Value* data = object.Find("data"); data != nullptr)
                {
                    entries.push_back({data, description});
                }
                return std::nullopt;
            };
            Ser::Object& envelope = tree.AsObject();
            if (std::optional<std::string> problem = entryOf(envelope, "its top level"))
            {
                return problem;
            }
            if (Ser::Value* sub = envelope.Find("sub"); sub != nullptr && sub->Kind() == Ser::ValueKind::Object)
            {
                for (Ser::Member& member : sub->AsObject().members)
                {
                    if (member.value.Kind() != Ser::ValueKind::Object)
                    {
                        continue;
                    }
                    if (std::optional<std::string> problem =
                            entryOf(member.value.AsObject(), std::format("sub-asset `{}`", member.key)))
                    {
                        return problem;
                    }
                }
            }
            return std::nullopt;
        }

        std::optional<std::string> ReadFileText(const std::filesystem::path& file)
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                return std::nullopt;
            }
            std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            if (stream.bad())
            {
                return std::nullopt;
            }
            return text;
        }

        // The asset path `path` names under `root`, normalised, or why a move refuses it.
        std::optional<std::string> AssetPathOf(const std::filesystem::path& root, std::string_view path,
                                               std::string_view which, std::string& assetPath)
        {
            if (path.empty())
            {
                return std::format("`{}` is empty", which);
            }
            if (path.starts_with(Detail::BuiltinPrefix))
            {
                return std::format("`{}` names a built-in asset, which is no file", which);
            }
            if (path.find('#') != std::string_view::npos)
            {
                return std::format("`{}` names a sub-asset: a move moves a whole file", which);
            }
            if (std::optional<std::string> problem = Detail::FilePathProblem(path))
            {
                return std::format("`{}` {}", which, *problem);
            }
            const std::optional<std::string> relative = RootRelative(root, (root / Utf8Path(path)).lexically_normal());
            if (!relative || *relative == ".")
            {
                return std::format("`{}` is outside the asset root '{}'", which, Utf8Text(root));
            }
            assetPath = *relative;
            return std::nullopt;
        }

        // A file to write back: where, and its new text.
        struct Rewritten
        {
            std::string assetPath;
            std::string text;
        };
    } // namespace

    MoveResult Assets::Move(const std::filesystem::path& root, std::string_view from, std::string_view to,
                            const TypeRegistry& registry, bool dryRun)
    {
        MoveResult result;
        const auto refuse = [&](std::string problem) {
            result.status = MoveStatus::Refused;
            result.problem = std::move(problem);
            return result;
        };

        const std::filesystem::path normalRoot = std::filesystem::absolute(root).lexically_normal();
        std::string fromPath;
        std::string toPath;
        if (std::optional<std::string> problem = AssetPathOf(normalRoot, from, "from", fromPath))
        {
            return refuse(std::move(*problem));
        }
        if (std::optional<std::string> problem = AssetPathOf(normalRoot, to, "to", toPath))
        {
            return refuse(std::move(*problem));
        }
        const std::filesystem::path fromFile = normalRoot / Utf8Path(fromPath);
        const std::filesystem::path toFile = normalRoot / Utf8Path(toPath);
        std::error_code error;
        if (!std::filesystem::is_regular_file(fromFile, error))
        {
            return refuse(std::format("`from` '{}' names no file", fromPath));
        }
        // A rename that changes only the case finds the file itself at `to` where the disk ignores case.
        const bool caseOnly = fromPath != toPath && std::filesystem::exists(toFile, error) &&
                              std::filesystem::equivalent(fromFile, toFile, error);
        if (!caseOnly && (std::filesystem::exists(toFile, error) || error))
        {
            return refuse(std::format("`to` '{}' exists already: a move never writes over a file", toPath));
        }

        // Every edit is made in memory first, so a dry run lists exactly what a move writes.
        std::vector<Rewritten> rewritten;
        std::optional<std::string> movedText;
        NullLogger quiet;
        ReferenceRewriter rewriter(fromPath, toPath);
        for (std::filesystem::recursive_directory_iterator it(normalRoot, error), end; !error && it != end;
             it.increment(error))
        {
            // An entry whose status cannot be read is no file this scan can read, and ends nothing.
            std::error_code entryError;
            if (!it->is_regular_file(entryError) || it->path().extension() != ".json5")
            {
                continue;
            }
            const std::optional<std::string> found = RootRelative(normalRoot, it->path().lexically_normal());
            if (!found)
            {
                continue;
            }
            // The moved file is named by the path it moves to, in both lists.
            const bool isMoved = *found == fromPath;
            const std::string& relative = isMoved ? toPath : *found;
            const std::optional<std::string> text = ReadFileText(it->path());
            if (!text)
            {
                result.unscanned.push_back({relative, "it could not be read"});
                continue;
            }
            Ser::ReadTreeResult read = Ser::ReadText(*text, *found, quiet);
            if (!read.tree)
            {
                result.unscanned.push_back({relative, read.diagnostics.empty()
                                                          ? std::string("it does not parse")
                                                          : Ser::FormatDiagnostic(read.diagnostics.front())});
                continue;
            }
            std::vector<ScanEntry> entries;
            if (std::optional<std::string> problem = EntriesOf(*read.tree, registry, entries))
            {
                result.unscanned.push_back({relative, std::move(*problem)});
                continue;
            }
            std::size_t count = 0;
            for (const ScanEntry& entry : entries)
            {
                count += rewriter.Rewrite(*entry.data, *entry.type);
            }
            if (count == 0)
            {
                continue;
            }
            result.references += count;
            std::string written = Ser::WriteText(*read.tree);
            result.changed.push_back(relative);
            if (isMoved)
            {
                movedText = std::move(written);
            }
            else
            {
                rewritten.push_back({relative, std::move(written)});
            }
        }
        if (error)
        {
            result.changed.clear();
            result.unscanned.clear();
            result.references = 0;
            return refuse(
                std::format("the asset root '{}' could not be scanned ({})", Utf8Text(normalRoot), error.message()));
        }
        std::ranges::sort(result.changed);
        std::ranges::sort(result.unscanned, {}, &UnscannedFile::path);
        if (dryRun)
        {
            return result;
        }

        const auto fail = [&](std::vector<std::string> done, std::string problem) {
            std::ranges::sort(done);
            result.status = MoveStatus::Failed;
            result.changed = std::move(done);
            result.problem = std::move(problem);
            return result;
        };
        // The file moves first, so a move that cannot happen leaves every other file as it was; it is renamed before
        // its references are written, so a rename that changes only the case writes into the renamed file.
        std::filesystem::create_directories(toFile.parent_path(), error);
        if (!error)
        {
            std::filesystem::rename(fromFile, toFile, error);
        }
        if (error)
        {
            return fail({}, std::format("'{}' could not be moved to '{}' ({})", fromPath, toPath, error.message()));
        }
        std::vector<std::string> done;
        if (movedText)
        {
            if (std::optional<std::string> problem = Detail::WriteAtomically(toFile, *movedText))
            {
                return fail(
                    {}, std::format("'{}' was moved, but its references could not be written: {}", toPath, *problem));
            }
            done.push_back(toPath);
        }
        for (const Rewritten& file : rewritten)
        {
            if (std::optional<std::string> problem =
                    Detail::WriteAtomically(normalRoot / Utf8Path(file.assetPath), file.text))
            {
                return fail(std::move(done), std::format("'{}' could not be written: {}", file.assetPath, *problem));
            }
            done.push_back(file.assetPath);
        }
        return result;
    }
} // namespace Fenrir
