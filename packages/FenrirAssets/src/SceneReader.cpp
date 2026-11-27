// The scene reader (ADR-0078): AssetStore::LoadScene, a scene envelope read into a new scene in two passes.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"
#include "Envelope.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/ReadTree.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

namespace Fenrir
{
    namespace
    {
        namespace Ser = Serialisation;

        using Detail::Append;

        // The version a component's or resource's entry was written at (1 when it has none), and whether its
        // `version` could be read.
        struct EntryVersion
        {
            std::uint32_t version = 1;
            bool readable = true;
        };

        // Takes `version` out of `entry`, an entry's object, and reads it; an entry that is not an object has none.
        EntryVersion TakeVersion(Ser::Value& entry)
        {
            if (entry.Kind() != Ser::ValueKind::Object)
            {
                return {};
            }
            std::vector<Ser::Member>& members = entry.AsObject().members;
            for (auto member = members.begin(); member != members.end(); ++member)
            {
                if (member->key != "version")
                {
                    continue;
                }
                const std::optional<std::uint64_t> read =
                    Detail::PositiveWhole(&member->value, std::numeric_limits<std::uint32_t>::max());
                if (!read)
                {
                    return {1, false};
                }
                members.erase(member);
                return {static_cast<std::uint32_t>(*read), true};
            }
            return {};
        }

        // One load's work: the scene being filled, where its types are found, its file's path for diagnostics, how
        // its asset handle fields are read, and the result being filled.
        struct SceneFiller
        {
            Scene& scene;
            const TypeRegistry& registry;
            std::string_view path;
            const std::function<Detail::ReferenceRead(std::string_view, const FieldInfo&)>& readReference;
            ILogger& logger;
            SceneLoadResult& result;

            // Each block's entity by its id's value: the first block holding an id.
            std::unordered_map<std::uint64_t, std::uint32_t> ids;
            // The unknown component types already warned of in this load.
            std::unordered_set<std::string> unknownWarned;

            // Logs one of the reader's own diagnostics and records it.
            void Report(Ser::DiagnosticSeverity severity, const Ser::Value* node, const Ser::Value& fallback,
                        std::string message)
            {
                Detail::Record(Detail::LocatedAt(severity, path, node, fallback, std::move(message)), logger,
                               result.diagnostics);
            }

            // Upgrades `tree`, an entry of `type` written at `version`, then fills `value`, an instance of the type,
            // from it: its references through the store, its `entity` fields through the id map. `where` names the
            // entry in a diagnostic.
            void Fill(Ser::Value& tree, const TypeDescription& type, const EntryVersion& version, void* value,
                      const std::string& where)
            {
                if (!version.readable)
                {
                    Report(Ser::DiagnosticSeverity::Error, tree.AsObject().Find("version"), tree,
                           std::format("{}expected `version`, a whole number from 1, found {}; the entry is read as "
                                       "version 1",
                                       where, Detail::Found(tree.AsObject().Find("version"))));
                    tree.AsObject().members.erase(
                        std::ranges::find(tree.AsObject().members, std::string("version"), &Ser::Member::key));
                }
                Ser::UpgradeResult upgraded = Ser::Upgrade(registry, type.name, version.version, tree, path, logger);
                Append(result.diagnostics, std::move(upgraded.diagnostics));

                EntityList& entities = scene.GetEntityList();
                Detail::ReferenceReading reading;
                Ser::Resolvers resolvers;
                resolvers.readReference = Detail::CheckedReferenceReader(readReference, where, reading);
                resolvers.readEntity = [&](std::string_view id, const FieldInfo&) -> std::optional<EntityValue> {
                    const std::optional<EntityId> parsed = ParseEntityId(id);
                    if (!parsed)
                    {
                        return std::nullopt;
                    }
                    const auto found = ids.find(parsed->value);
                    if (found == ids.end())
                    {
                        return std::nullopt;
                    }
                    return EntityValue{found->second, &entities};
                };
                Ser::BridgeResult bridged = Ser::FromTree(tree, type, registry, resolvers, value, path, logger);
                Append(result.diagnostics, std::move(bridged.diagnostics));
                result.fieldsDropped += bridged.defaulted + bridged.unknownFields;
                result.referencesEmptied += bridged.unresolved;
                result.referencesEmptied += Detail::ReportEmptied(reading, tree, path, logger, result.diagnostics);
            }

            // The id a block names, or none, with an error, when it has none or one that does not parse.
            std::optional<EntityId> BlockId(const Ser::Object& block, const Ser::Value& at)
            {
                const Ser::Value* id = block.Find("id");
                if (id != nullptr && id->Kind() == Ser::ValueKind::String)
                {
                    if (std::optional<EntityId> parsed = ParseEntityId(id->AsString()))
                    {
                        return parsed;
                    }
                }
                Report(Ser::DiagnosticSeverity::Error, id, at,
                       std::format("expected `id`, an entity id of one to twelve lower-case Crockford base32 "
                                   "characters, found {}; the entity loads with no id, and a save mints one",
                                   Detail::Found(id)));
                return std::nullopt;
            }

            // Fills the entity `entity`, reserved for `block`: completes it, gives it its id `id` when it `holdsId`,
            // adds its components and appends it to its parent. `filled` holds every entity of an earlier block, which
            // a `parent` may name.
            void FillBlock(Ser::Object& block, const Ser::Value& at, std::uint32_t entity, std::optional<EntityId> id,
                           bool holdsId, const std::unordered_set<std::uint32_t>& filled)
            {
                EntityList& entities = scene.GetEntityList();
                entities.CompleteEntity(entity);
                Entity handle = entities.GetEntity(entity);
                ++result.entities;
                const std::string spelled = id ? SpellEntityId(*id) : std::string("?");
                if (holdsId)
                {
                    handle.AddComponent<EntityId>(*id);
                }

                UnknownComponents unknown;
                Ser::Value* components = block.Find("components");
                if (components != nullptr && components->Kind() != Ser::ValueKind::Object)
                {
                    Report(Ser::DiagnosticSeverity::Error, components, at,
                           std::format("entity `{}`: expected `components`, an object of entries by type name, found "
                                       "{}; the entity loads with none",
                                       spelled, Detail::Found(components)));
                    components = nullptr;
                }
                if (components != nullptr)
                {
                    for (Ser::Member& member : components->AsObject().members)
                    {
                        FillComponent(member, *components, entity, spelled, unknown);
                    }
                }
                if (!unknown.components.empty())
                {
                    handle.AddComponent<UnknownComponents>(std::move(unknown));
                }

                const Ser::Value* parent = block.Find("parent");
                if (parent == nullptr)
                {
                    result.roots.push_back(handle);
                    return;
                }
                const std::optional<EntityId> parentId =
                    parent->Kind() == Ser::ValueKind::String ? ParseEntityId(parent->AsString()) : std::nullopt;
                const auto found = parentId ? ids.find(parentId->value) : ids.end();
                if (found == ids.end() || !filled.contains(found->second))
                {
                    Report(Ser::DiagnosticSeverity::Error, parent, at,
                           std::format("entity `{}`: expected `parent`, the id of a block before it, found {}; the "
                                       "entity loads at the top",
                                       spelled, Detail::Found(parent)));
                    result.roots.push_back(handle);
                    return;
                }
                entities.GetEntity(found->second).AddChild(handle);
            }

            // Adds the component `member` holds to `entity`, or keeps it in `unknown` when the registry lacks its
            // type as a component.
            void FillComponent(Ser::Member& member, const Ser::Value& at, std::uint32_t entity,
                               const std::string& spelled, UnknownComponents& unknown)
            {
                if (std::ranges::find(Detail::SpelledApart, member.key) != std::end(Detail::SpelledApart))
                {
                    Report(Ser::DiagnosticSeverity::Warning, &member.value, at,
                           std::format("entity `{}`: `{}` is spelled by the block's `parent`, `id` or `instance`, not "
                                       "as a component; it is ignored",
                                       spelled, member.key));
                    return;
                }
                const TypeDescription* type = registry.Find(member.key);
                const std::optional<ComponentType> component = type != nullptr && type->role == Role::Component
                                                                   ? ComponentType::ForName(registry, type->name)
                                                                   : std::nullopt;
                if (!component)
                {
                    // Kept whole as it was read, `version` and all, so a save writes it back unchanged (ADR-0078 §9);
                    // the version is noted beside it, 1 when it has none it can read.
                    if (unknownWarned.insert(member.key).second)
                    {
                        Report(Ser::DiagnosticSeverity::Warning, &member.value, at,
                               std::format("entity `{}`: `{}` is no component type the type registry has; it is kept "
                                           "as it is, and written back unchanged on save",
                                           spelled, member.key));
                    }
                    std::vector<std::byte> tree = Ser::EncodeTree(member.value, Ser::Trivia::Keep);
                    unknown.components.push_back({member.key, TakeVersion(member.value).version, std::move(tree)});
                    ++result.unknownComponentsKept;
                    return;
                }

                const EntryVersion version = TakeVersion(member.value);
                EntityList& entities = scene.GetEntityList();
                // Name and Transform are there already, from CompleteEntity; any other is added default.
                entities.Add(*component, entity);
                Fill(member.value, *type, version, entities.Get(*component, entity),
                     std::format("entity `{}`, `{}`: ", spelled, type->name));
                ++result.components;
            }

            // Adds or fills each scene resource `resources` holds, after every entity, so an `entity` field in one
            // names an entity of the file (ADR-0078 §7).
            void FillResources(Ser::Value& resources)
            {
                for (Ser::Member& member : resources.AsObject().members)
                {
                    const EntryVersion version = TakeVersion(member.value);
                    const TypeDescription* type = registry.Find(member.key);
                    void* resource = nullptr;
                    if (type != nullptr && type->role == Role::SceneResource)
                    {
                        resource = scene.FindResource(registry, type->name);
                        if (resource == nullptr)
                        {
                            resource = scene.AddResource(registry, type->name);
                        }
                    }
                    if (resource == nullptr)
                    {
                        Report(Ser::DiagnosticSeverity::Warning, &member.value, resources,
                               std::format("`{}` is no scene resource type the type registry has; it is skipped",
                                           member.key));
                        ++result.resourcesSkipped;
                        continue;
                    }
                    Fill(member.value, *type, version, resource, std::format("resource `{}`: ", type->name));
                }
            }

            // Fills the scene from `data`, a scene envelope's: the entities in two passes, then the resources.
            void FillScene(Ser::Value& data)
            {
                Ser::Value* blocks = data.AsObject().Find("entities");
                if (blocks != nullptr && blocks->Kind() != Ser::ValueKind::Array)
                {
                    Report(Ser::DiagnosticSeverity::Error, blocks, data,
                           std::format("expected `entities`, an array of blocks, found {}; no entity is loaded",
                                       Detail::Found(blocks)));
                    blocks = nullptr;
                }

                // Pass one: an entity for every block, and the id map, so a field or a `parent` names any block.
                struct Reserved
                {
                    Ser::Object* block = nullptr;
                    const Ser::Value* at = nullptr;
                    std::uint32_t entity = 0;
                    // The block's id, when it parsed; the entity holds it only when no earlier block has it.
                    std::optional<EntityId> id;
                    bool holdsId = false;
                };
                std::vector<Reserved> reserved;
                EntityList& entities = scene.GetEntityList();
                if (blocks != nullptr)
                {
                    for (Ser::Element& element : blocks->AsArray().elements)
                    {
                        if (element.value.Kind() != Ser::ValueKind::Object)
                        {
                            Report(Ser::DiagnosticSeverity::Error, &element.value, *blocks,
                                   std::format("expected a block, an object of `id`, `parent` and `components`, found "
                                               "{}; it is not loaded",
                                               Detail::Found(&element.value)));
                            continue;
                        }
                        Ser::Object& block = element.value.AsObject();
                        std::optional<EntityId> id = BlockId(block, element.value);
                        const std::uint32_t entity = entities.ReserveEntity();
                        bool holdsId = id.has_value();
                        if (id && !ids.emplace(id->value, entity).second)
                        {
                            // No two entities of the scene share an id: this one loads with none, as one whose id
                            // did not parse does.
                            Report(Ser::DiagnosticSeverity::Error, block.Find("id"), element.value,
                                   std::format("the id `{}` is an earlier block's too; references to it name that "
                                               "block, and this entity loads with no id, so a save mints one",
                                               SpellEntityId(*id)));
                            holdsId = false;
                        }
                        reserved.push_back({&block, &element.value, entity, id, holdsId});
                    }
                }

                // Pass two, in file order: each entity completed and filled, then appended to its parent.
                std::unordered_set<std::uint32_t> filled;
                for (const Reserved& entry : reserved)
                {
                    FillBlock(*entry.block, *entry.at, entry.entity, entry.id, entry.holdsId, filled);
                    filled.insert(entry.entity);
                }

                Ser::Value* resources = data.AsObject().Find("resources");
                if (resources != nullptr && resources->Kind() != Ser::ValueKind::Object)
                {
                    Report(Ser::DiagnosticSeverity::Error, resources, data,
                           std::format("expected `resources`, an object of entries by type name, found {}; no "
                                       "resource is loaded",
                                       Detail::Found(resources)));
                    resources = nullptr;
                }
                if (resources != nullptr)
                {
                    FillResources(*resources);
                }
            }
        };

        // Why a scene envelope's root, read from the file, is refused: a located error's message, at `node`; or none,
        // with its `data` in `data`.
        struct EnvelopeProblem
        {
            const Ser::Value* node = nullptr;
            std::string message;
        };

        std::optional<EnvelopeProblem> CheckSceneEnvelope(Ser::Value& root, Ser::Value*& data)
        {
            if (root.Kind() != Ser::ValueKind::Object)
            {
                return EnvelopeProblem{&root,
                                       std::format("expected a scene envelope object, found {}", Detail::Found(&root))};
            }
            Ser::Object& envelope = root.AsObject();
            const Ser::Value* format = envelope.Find("format");
            if (std::optional<std::string> problem = Detail::FormatProblem(format))
            {
                return EnvelopeProblem{format, std::move(*problem)};
            }
            const Ser::Value* type = envelope.Find("type");
            if (type == nullptr || type->Kind() != Ser::ValueKind::String || type->AsString() != Detail::SceneTypeName)
            {
                return EnvelopeProblem{
                    type, std::format("expected `type` \"{}\", found {}", Detail::SceneTypeName, Detail::Found(type))};
            }
            const Ser::Value* version = envelope.Find("version");
            if (Detail::PositiveWhole(version, Detail::SceneVersion) == std::nullopt)
            {
                return EnvelopeProblem{version,
                                       std::format("expected `version`, the scene layout's, from 1 up to {}, found {}",
                                                   Detail::SceneVersion, Detail::Found(version))};
            }
            data = envelope.Find("data");
            if (data == nullptr || data->Kind() != Ser::ValueKind::Object)
            {
                return EnvelopeProblem{data, std::format("expected `data`, an object of `resources` and `entities`, "
                                                         "found {}",
                                                         Detail::Found(data))};
            }
            return std::nullopt;
        }
    } // namespace

    SceneLoadResult AssetStore::LoadScene(App& app, std::string_view path, std::string_view name)
    {
        return ReadSceneFile(
            path, [&]() -> Scene& { return app.CreateScene(name.empty() ? std::string(path) : std::string(name)); });
    }

    SceneLoadResult AssetStore::ReadSceneFile(std::string_view path, const std::function<Scene&()>& makeScene)
    {
        SceneLoadResult result;
        std::string assetPath;
        std::string fileKey;
        const std::optional<std::string> pathProblem =
            Detail::WholeFileLoadProblem(m_state->root, path, assetPath, fileKey);
        // The load fails with `diagnostic`, logged here once, and no scene made.
        const auto fail = [&](Ser::Diagnostic diagnostic) -> SceneLoadResult {
            m_state->logger->Error("Could not load scene '{}': {}", path, Ser::FormatDiagnostic(diagnostic));
            result.status = LoadStatus::Failed;
            result.diagnostics.push_back(std::move(diagnostic));
            return std::move(result);
        };
        const auto failWith = [&](std::string message) {
            return fail({.severity = Ser::DiagnosticSeverity::Error, .path = fileKey, .message = std::move(message)});
        };

        if (pathProblem)
        {
            return failWith(*pathProblem);
        }
        Detail::AssetSource& source = *m_state->source;
        const std::optional<std::vector<std::byte>> bytes = source.Read(assetPath);
        if (!bytes)
        {
            return failWith(source.Exists(assetPath) ? "the file could not be read" : "there is no file at this path");
        }

        Ser::ReadTreeResult tree = Ser::ReadTree(*bytes, fileKey, *m_state->logger);
        if (!tree.tree)
        {
            // ReadTree logged its own diagnostics; the store's error says the load failed.
            m_state->logger->Error("Could not load scene '{}': the file is neither JSON5 nor the tree encoding", path);
            result.status = LoadStatus::Failed;
            result.diagnostics = std::move(tree.diagnostics);
            return result;
        }
        Append(result.diagnostics, std::move(tree.diagnostics));
        Ser::Value* data = nullptr;
        if (std::optional<EnvelopeProblem> problem = CheckSceneEnvelope(*tree.tree, data))
        {
            return fail(Detail::LocatedAt(Ser::DiagnosticSeverity::Error, fileKey, problem->node, *tree.tree,
                                          std::move(problem->message)));
        }

        Scene& scene = makeScene();
        result.scene = &scene;
        const std::uint32_t owner = OwnerOf(AssetOwner(scene));
        const std::function<Detail::ReferenceRead(std::string_view, const FieldInfo&)> readReference =
            [&](std::string_view reference, const FieldInfo& field) {
                return ReadFileReference(assetPath, owner, reference, field);
            };
        SceneFiller filler{scene, *m_state->types, fileKey, readReference, *m_state->logger, result};
        filler.FillScene(*data);
        result.status = result.diagnostics.empty() ? LoadStatus::Succeeded : LoadStatus::Partial;
        return result;
    }
} // namespace Fenrir
