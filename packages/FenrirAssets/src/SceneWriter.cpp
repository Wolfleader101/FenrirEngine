// The scene writer (ADR-0078, ADR-0079, ADR-0080): AssetStore::SaveScene, a scene, or a subtree of one, to its scene
// envelope, each instance in it as one block naming its source.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"
#include "Envelope.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"
#include "SceneInstances.hpp"

namespace Fenrir
{
    namespace
    {
        namespace Ser = Serialisation;

        constexpr std::string_view NameType = "Fenrir::Name";
        constexpr std::string_view TransformType = "Fenrir::Transform";

        // One described component type a block writes when its entity has one.
        struct WrittenType
        {
            const TypeDescription* type = nullptr;
            ComponentType component;
        };

        // Where an entry goes among `components`: Name, then Transform, then the rest by canonical name, so the same
        // scene writes the same bytes (ADR-0077 §1).
        int Rank(std::string_view name)
        {
            return name == NameType ? 0 : name == TransformType ? 1 : 2;
        }

        // Every described component type a block can write, in the order it writes them.
        std::vector<WrittenType> WrittenTypes(const TypeRegistry& registry)
        {
            std::vector<WrittenType> written;
            for (const TypeDescription* type : registry.Types(Role::Component))
            {
                if (type->transient ||
                    std::ranges::find(Detail::SpelledApart, type->name) != std::end(Detail::SpelledApart))
                {
                    continue;
                }
                // A component type no package attached a ComponentType to cannot be reached on an entity, so it is
                // never found there to write.
                if (std::optional<ComponentType> component = ComponentType::ForName(registry, type->name))
                {
                    written.push_back({type, *component});
                }
            }
            std::ranges::sort(written, [](const WrittenType& a, const WrittenType& b) {
                const int rankA = Rank(a.type->name);
                const int rankB = Rank(b.type->name);
                return rankA != rankB ? rankA < rankB : a.type->name < b.type->name;
            });
            return written;
        }

        // How many entities are in the subtree `entity` roots, itself included.
        std::size_t SubtreeSize(const Entity& entity)
        {
            std::size_t size = 1;
            entity.ForEachChild([&](const Entity& child) { size += SubtreeSize(child); });
            return size;
        }

        // The source an instance's root names.
        std::string SourceOf(const Entity& root)
        {
            return std::string(root.GetComponent<Instance>().source);
        }

        // The entities one save writes, and how each instance among them matches its source.
        struct Collector
        {
            Detail::InstanceBaselines& baselines;
            // Depth-first, each entity's children after it in their sibling order; the entities a host added inside an
            // instance after the instance's block, in the order the instance holds them.
            std::vector<Entity> written;
            // The entities left out: each tagged Transient, and every entity below one.
            std::size_t skipped = 0;
            // Each instance's match with its baseline, by its root's entity id.
            std::unordered_map<std::uint32_t, Detail::InstanceMatch> matches;

            // Appends the subtree `entity` roots. An instance's root is one block; what is below it is its source's,
            // written as the block's diff, but for the entities a host added, which are blocks of their own.
            void Collect(const Entity& entity)
            {
                if (entity.HasComponent<Transient>())
                {
                    skipped += SubtreeSize(entity);
                    return;
                }
                written.push_back(entity);
                if (entity.HasComponent<Instance>())
                {
                    const Detail::InstanceBaseline* baseline = baselines.For(SourceOf(entity));
                    Detail::InstanceMatch match =
                        Detail::MatchInstance(entity, baseline, [&](const Entity& added) { Collect(added); });
                    matches.emplace(entity.GetId(), std::move(match));
                    return;
                }
                entity.ForEachChild([&](const Entity& child) { Collect(child); });
            }
        };

        // Records in `paths` the slash path of each entity inside the instance `root` tops, whose own path is
        // `prefix` (ADR-0080 §5): the path, `/`, and the entity's id in the source, chained through each nested
        // instance's root. An entity with no id has none a file could name; one tagged Transient, and every entity
        // below it, is no entity of any file.
        void MapInstance(const Entity& scope, const std::string& prefix,
                         std::unordered_map<std::uint32_t, std::string>& paths)
        {
            scope.ForEachChild([&](const Entity& entity) {
                if (entity.HasComponent<Transient>())
                {
                    return;
                }
                const bool named = entity.HasComponent<EntityId>() && entity.GetComponent<EntityId>().value != 0;
                std::string path = named ? std::format("{}/{}", prefix, SpellEntityId(entity.GetComponent<EntityId>()))
                                         : std::string();
                if (!entity.HasComponent<Instance>())
                {
                    // Still the same instance: its ids are in this scope.
                    MapInstance(entity, prefix, paths);
                }
                else if (named)
                {
                    MapInstance(entity, path, paths);
                }
                if (named)
                {
                    paths.emplace(entity.GetId(), std::move(path));
                }
            });
        }

        // An entry's object: `version` when it is above 1, then `fields`, the type's fields as ToTree gave them.
        Ser::Value Entry(std::uint32_t version, Ser::Value fields)
        {
            if (version <= 1)
            {
                return fields;
            }
            Ser::Object entry;
            entry.Add("version", version);
            for (Ser::Member& member : fields.AsObject().members)
            {
                entry.members.push_back(std::move(member));
            }
            return entry;
        }

        // Sorts `entries` into a block's order: Name, then Transform, then the rest by name.
        void SortEntries(std::vector<Ser::Member>& entries)
        {
            std::ranges::stable_sort(entries, [](const Ser::Member& a, const Ser::Member& b) {
                const int rankA = Rank(a.key);
                const int rankB = Rank(b.key);
                return rankA != rankB ? rankA < rankB : a.key < b.key;
            });
        }

        // Takes the diff of an instance against its baseline (ADR-0080 §4), as trees, so a value equal to the source's
        // is never written: each entity of the instance is compared with the baseline's entity of its key, component
        // by component and field by field.
        struct Differ
        {
            const TypeRegistry& registry;
            const std::vector<WrittenType>& types;
            // The host scene's entities, and the baselines'.
            EntityList& live;
            EntityList& base;
            // How the file writes what a field names: an entity by its id or slash path, an asset by its path.
            const Ser::Resolvers& file;
            // How an asset is named for a comparison: by its path, with no warning.
            const Ser::Resolvers& assetPaths;
            // The prefix naming the entry being written, for the file's warnings.
            std::string& where;
            ILogger& logger;
            std::string_view path;
            std::vector<Ser::Diagnostic>& diagnostics;

            // How a comparison names what an `entity` field holds: `@` for the instance's root, `@/<key>` for an
            // entity of the instance, and anything else by a spelling the other side never has, so a field naming
            // the same place in both compares equal, though each names an entity of its own scene.
            static Ser::Resolvers Relative(const Ser::Resolvers& assetPaths, EntityList& list, Entity root,
                                           const std::unordered_map<std::uint32_t, std::string>& keys,
                                           std::string_view elsewhere)
            {
                Ser::Resolvers relative;
                relative.writeReference = assetPaths.writeReference;
                relative.writeEntity = [&list, root, &keys, elsewhere](EntityValue target,
                                                                       const FieldInfo&) -> std::string {
                    if (target.list == static_cast<void*>(&list))
                    {
                        if (target.id == root.GetId())
                        {
                            return "@";
                        }
                        if (const auto found = keys.find(target.id); found != keys.end())
                        {
                            return "@/" + found->second;
                        }
                    }
                    return std::format("{}{}", elsewhere, target.id);
                };
                return relative;
            }

            // Writes into `entry` what `liveEntity` changes of `baseEntity`: `components`, each entry the entity has
            // that the baseline's lacks whole, or that differs only by its differing fields; then `removed`, the
            // types of the baseline's it lacks. `label` names the entity in warnings. How many entries it wrote.
            std::size_t DiffEntity(const Entity& liveEntity, const Entity& baseEntity, const Ser::Resolvers& liveNames,
                                   const Ser::Resolvers& baseNames, const std::string& label, Ser::Object& entry)
            {
                std::vector<Ser::Member> entries;
                Ser::Array removed;
                for (const WrittenType& type : types)
                {
                    const void* liveValue = live.Get(type.component, liveEntity.GetId());
                    const void* baseValue = base.Get(type.component, baseEntity.GetId());
                    if (liveValue == nullptr)
                    {
                        if (baseValue != nullptr)
                        {
                            removed.Add(std::string(type.type->name));
                        }
                        continue;
                    }
                    where = std::format("entity `{}`, `{}`: ", label, type.type->name);
                    if (baseValue == nullptr)
                    {
                        entries.push_back(
                            {std::string(type.type->name),
                             Entry(type.type->version, Ser::ToTree(liveValue, *type.type, registry, file))});
                        continue;
                    }
                    const Ser::Value liveTree = Ser::ToTree(liveValue, *type.type, registry, liveNames);
                    const Ser::Value baseTree = Ser::ToTree(baseValue, *type.type, registry, baseNames);
                    if (liveTree == baseTree)
                    {
                        continue;
                    }
                    // Against the baseline's value, the fields that differ in bytes; one naming an entity always does,
                    // since the two are of different scenes, so it is kept only when the comparison's trees differ.
                    Ser::Value changed = Ser::ToTree(liveValue, *type.type, registry, file, nullptr, baseValue);
                    std::erase_if(changed.AsObject().members, [&](const Ser::Member& member) {
                        const Ser::Value* was = baseTree.AsObject().Find(member.key);
                        const Ser::Value* is = liveTree.AsObject().Find(member.key);
                        return was == nullptr ? is == nullptr : is != nullptr && *is == *was;
                    });
                    if (!changed.AsObject().members.empty())
                    {
                        entries.push_back(
                            {std::string(type.type->name), Entry(type.type->version, std::move(changed))});
                    }
                }
                DiffUnknown(liveEntity, baseEntity, label, entries, removed);
                const std::size_t count = entries.size();
                if (!entries.empty())
                {
                    SortEntries(entries);
                    Ser::Object components;
                    components.members = std::move(entries);
                    entry.Add("components", std::move(components));
                }
                if (!removed.elements.empty())
                {
                    entry.Add("removed", std::move(removed));
                }
                return count;
            }

            // Adds to `entries` each component a load kept opaque on `liveEntity` that `baseEntity` does not hold as
            // it is, as it was read, and to `removed` each `baseEntity` holds and `liveEntity` does not.
            void DiffUnknown(const Entity& liveEntity, const Entity& baseEntity, const std::string& label,
                             std::vector<Ser::Member>& entries, Ser::Array& removed)
            {
                static const std::vector<UnknownComponent> none;
                const std::vector<UnknownComponent>& liveKept =
                    liveEntity.HasComponent<UnknownComponents>()
                        ? liveEntity.GetComponent<UnknownComponents>().components
                        : none;
                const std::vector<UnknownComponent>& baseKept =
                    baseEntity.HasComponent<UnknownComponents>()
                        ? baseEntity.GetComponent<UnknownComponents>().components
                        : none;
                for (const UnknownComponent& kept : liveKept)
                {
                    const auto was = std::ranges::find(baseKept, kept.typeName, &UnknownComponent::typeName);
                    if ((was != baseKept.end() && was->tree == kept.tree) ||
                        std::ranges::find(entries, kept.typeName, &Ser::Member::key) != entries.end())
                    {
                        continue;
                    }
                    Ser::ReadTreeResult tree = Ser::DecodeTree(kept.tree, path, logger);
                    if (!tree.tree)
                    {
                        Detail::Record({.severity = Ser::DiagnosticSeverity::Warning,
                                        .path = std::string(path),
                                        .message = std::format("entity `{}`: the kept `{}` is not one whole tree "
                                                               "encoding; it is left out",
                                                               label, kept.typeName)},
                                       logger, diagnostics);
                        continue;
                    }
                    entries.push_back({kept.typeName, std::move(*tree.tree)});
                }
                for (const UnknownComponent& kept : baseKept)
                {
                    if (std::ranges::find(liveKept, kept.typeName, &UnknownComponent::typeName) == liveKept.end())
                    {
                        removed.Add(kept.typeName);
                    }
                }
            }

            // Writes into `block`, the instance `root`'s, its diff against `match`'s baseline: the root's
            // `components` and `removed`, then `overrides` by key, then `removedEntities`. `label` is the root's id
            // in the file. How many entries it wrote, the root's in `rootEntries`.
            std::size_t Diff(Ser::Object& block, const Entity& root, const Detail::InstanceMatch& match,
                             const std::string& label, std::size_t& rootEntries)
            {
                const Detail::InstanceBaseline& baseline = *match.baseline;
                const Ser::Resolvers liveNames = Relative(assetPaths, live, root, match.keyOf, "live#");
                const Ser::Resolvers baseNames = Relative(assetPaths, base, baseline.root, baseline.keyOf, "source#");
                rootEntries = DiffEntity(root, baseline.root, liveNames, baseNames, label, block);
                std::size_t written = rootEntries;

                Ser::Object overrides;
                for (const auto& [key, baseEntity] : baseline.keyed)
                {
                    const auto liveEntity = match.liveByKey.find(key);
                    if (liveEntity == match.liveByKey.end() || match.quiet.contains(key))
                    {
                        continue;
                    }
                    Ser::Object entry;
                    written += DiffEntity(liveEntity->second, baseEntity, liveNames, baseNames,
                                          std::format("{}/{}", label, key), entry);
                    if (!entry.members.empty())
                    {
                        overrides.Add(key, std::move(entry));
                    }
                }
                if (!overrides.members.empty())
                {
                    block.Add("overrides", std::move(overrides));
                }
                if (!match.removed.empty())
                {
                    Ser::Array removed;
                    for (const std::string& key : match.removed)
                    {
                        removed.Add(key);
                    }
                    block.Add("removedEntities", std::move(removed));
                }
                return written;
            }
        };
    } // namespace

    SceneSaveResult AssetStore::SaveScene(Scene& scene, std::string_view path)
    {
        return WriteSceneFile(scene, std::nullopt, path);
    }

    SceneSaveResult AssetStore::SaveScene(Scene& scene, Entity root, std::string_view path)
    {
        return WriteSceneFile(scene, root, path);
    }

    SceneSaveResult AssetStore::WriteSceneFile(Scene& scene, std::optional<Entity> root, std::string_view path)
    {
        const std::string named(path);
        std::filesystem::path target;
        if (std::optional<std::string> problem = Detail::SavePathProblem(m_state->root, path, target))
        {
            SceneSaveResult result;
            FailSceneWrite(scene, named, std::move(*problem), result);
            return result;
        }
        Ser::Value envelope;
        SceneSaveResult result = WriteSceneTree(scene, root, named, envelope);
        if (result.status == SaveStatus::Failed)
        {
            return result;
        }
        if (const std::optional<std::string> problem = Detail::WriteAtomically(target, Ser::WriteText(envelope)))
        {
            FailSceneWrite(scene, named, *problem, result);
        }
        return result;
    }

    void AssetStore::FailSceneWrite(const Scene& scene, const std::string& named, std::string message,
                                    SceneSaveResult& result)
    {
        m_state->logger->Error("Could not save scene '{}' to '{}': {}", scene.GetName(), named, message);
        result.status = SaveStatus::Failed;
        result.diagnostics.push_back(
            {.severity = Ser::DiagnosticSeverity::Error, .path = named, .message = std::move(message)});
    }

    SceneSaveResult AssetStore::WriteSceneTree(Scene& scene, std::optional<Entity> root, const std::string& named,
                                               Ser::Value& envelope)
    {
        SceneSaveResult result;
        const auto fail = [&](std::string message) -> SceneSaveResult {
            FailSceneWrite(scene, named, std::move(message), result);
            return std::move(result);
        };

        const TypeRegistry& registry = *m_state->types;
        EntityList& entities = scene.GetEntityList();
        if (root && !Detail::LiveIn(entities, *root))
        {
            return fail("the subtree's root is no live entity of the scene");
        }
        if (root && root->HasComponent<Transient>())
        {
            return fail("the subtree's root is tagged Transient, which a scene file never holds");
        }

        // The entities the file holds: the subtree's, or every root's in the order of their entity ids, so a scene
        // saved twice lists them alike. Each instance's source is expanded fresh, once a save, to diff it against.
        Detail::InstanceBaselines baselines(
            [this](Scene& into, std::string_view source) { return ExpandInstance(into, source); });
        Collector collector{baselines};
        std::vector<Entity>& written = collector.written;
        if (root)
        {
            collector.Collect(*root);
        }
        else
        {
            std::vector<std::uint32_t> roots;
            entities.ForEachEntity([&](auto handle) {
                const auto id = static_cast<std::uint32_t>(handle);
                const Entity entity = entities.GetEntity(id);
                if (entity.HasComponent<Relationship>() && !entity.GetComponent<Relationship>().parent.IsValid())
                {
                    roots.push_back(id);
                }
            });
            std::ranges::sort(roots);
            for (const std::uint32_t top : roots)
            {
                collector.Collect(entities.GetEntity(top));
            }
        }
        result.entitiesSkipped = collector.skipped;

        // Every entity the file holds has an id unique within it before any block is written, so an `entity` field
        // names one written after its own block too. An id is kept by the first entity in file order that has it; a
        // later one, and an entity with none, is minted one no entity of the scene has, so a subtree's minted ids
        // stay apart from the rest of its scene too (ADR-0078 §2).
        std::unordered_set<std::uint64_t> held;
        entities.ForEachEntity([&](auto handle) {
            const Entity entity = entities.GetEntity(static_cast<std::uint32_t>(handle));
            if (entity.HasComponent<EntityId>())
            {
                held.insert(entity.GetComponent<EntityId>().value);
            }
        });
        std::unordered_set<std::uint64_t> taken;
        std::unordered_map<std::uint32_t, std::string> ids;
        for (const Entity& entity : written)
        {
            EntityId id = entity.HasComponent<EntityId>() ? entity.GetComponent<EntityId>() : EntityId{};
            if (id.value == 0 || !taken.insert(id.value).second)
            {
                do
                {
                    id = MintEntityId();
                } while (held.contains(id.value) || !taken.insert(id.value).second);
                if (entity.HasComponent<EntityId>())
                {
                    entity.GetComponent<EntityId>() = id;
                }
                else
                {
                    Entity(entity).AddComponent<EntityId>(id);
                }
            }
            ids.emplace(entity.GetId(), SpellEntityId(id));
        }
        // An entity inside an instance the file holds is named by its slash path, once every instance's root has its
        // id.
        for (const Entity& entity : written)
        {
            if (entity.HasComponent<Instance>())
            {
                MapInstance(entity, ids.at(entity.GetId()), ids);
            }
        }

        // An asset handle field is written as its asset's path, as a data asset's is; an `entity` field as its
        // target's id, or `null` when the file holds no such entity, since a load could resolve nothing else. Each
        // `null` is warned of, naming `where`, the entry being written.
        std::string where;
        const Ser::Resolvers assetPaths = Detail::WritingResolvers(*m_state);
        Ser::Resolvers resolvers;
        resolvers.writeReference = Detail::CheckedReferenceWriter(assetPaths, where, named, *m_state->logger,
                                                                  result.diagnostics, result.nullReferences);
        resolvers.writeEntity = [&](EntityValue target, const FieldInfo& field) -> std::string {
            if (target.list == static_cast<void*>(&entities))
            {
                if (const auto found = ids.find(target.id); found != ids.end())
                {
                    return found->second;
                }
            }
            Detail::WarnWrittenNull(
                std::format("{}`{}` names an entity the file does not hold (one tagged Transient or "
                            "below one, one outside the subtree saved, one destroyed, or one of another scene); it "
                            "is written null",
                            where, field.name),
                named, *m_state->logger, result.diagnostics, result.nullReferences);
            return {};
        };

        // Resources come before entities in the file but are applied after them on load, so an `entity` field in one
        // resolves (ADR-0078 §7); name order keeps the bytes stable. A subtree's file holds none: the scene's
        // resources are not the subtree's.
        Ser::Object resources;
        std::vector<const TypeDescription*> resourceTypes;
        if (!root)
        {
            resourceTypes = registry.Types(Role::SceneResource);
        }
        std::ranges::sort(resourceTypes, {}, &TypeDescription::name);
        for (const TypeDescription* type : resourceTypes)
        {
            const void* resource = type->transient ? nullptr : scene.FindResource(registry, type->name);
            if (resource == nullptr)
            {
                continue;
            }
            where = std::format("resource `{}`: ", type->name);
            resources.Add(std::string(type->name),
                          Entry(type->version, Ser::ToTree(resource, *type, registry, resolvers)));
        }

        const std::vector<WrittenType> types = WrittenTypes(registry);
        Differ differ{registry,  types,
                      entities,  baselines.GetScene().GetEntityList(),
                      resolvers, assetPaths,
                      where,     *m_state->logger,
                      named,     result.diagnostics};
        Ser::Array blocks;
        for (const Entity& entity : written)
        {
            const std::uint32_t id = entity.GetId();
            Ser::Object block;
            block.Add("id", ids.at(id));
            // The subtree's root is the file's root, whatever its parent in the scene.
            if (const Entity parent = entity.GetComponent<Relationship>().parent; parent.IsValid() && entity != root)
            {
                block.Add("parent", ids.at(parent.GetId()));
            }
            // An instance is its source and a diff: the block names the source, and holds only what the instance
            // changes of a fresh expansion of it (ADR-0080 §4).
            if (entity.HasComponent<Instance>())
            {
                const std::string source = SourceOf(entity);
                block.Add("instance", source);
                const Detail::InstanceMatch& match = collector.matches.at(id);
                std::size_t rootEntries = 0;
                if (match.baseline != nullptr)
                {
                    result.componentsWritten += differ.Diff(block, entity, match, ids.at(id), rootEntries);
                }
                else
                {
                    // With nothing to diff against, the block names its source alone: what it changed is lost.
                    Detail::Record({.severity = Ser::DiagnosticSeverity::Warning,
                                    .path = named,
                                    .message = std::format("entity `{}`: the instance's source `{}` did not load, so "
                                                           "no change the instance makes to it is written",
                                                           ids.at(id), source)},
                                   *m_state->logger, result.diagnostics);
                }
                blocks.Add(std::move(block));
                ++result.entitiesWritten;
                const std::size_t count = entities.ComponentCountOf(id);
                result.componentsSkipped += count - std::min(count, rootEntries);
                continue;
            }
            std::vector<Ser::Member> entries;
            std::size_t reached = 0;
            for (const WrittenType& entry : types)
            {
                const void* component = entities.Get(entry.component, id);
                if (component == nullptr)
                {
                    continue;
                }
                ++reached;
                where = std::format("entity `{}`, `{}`: ", ids.at(id), entry.type->name);
                // An entry at its type's defaults is `{}`, never left out: the file says the entity has it.
                entries.push_back(
                    {std::string(entry.type->name),
                     Entry(entry.type->version, Ser::ToTree(component, *entry.type, registry, resolvers))});
            }
            // The components a load kept opaque go back as they were read, `version` and all, among the rest in name
            // order (ADR-0078 §9). The holder is no entry of its own, so it is counted as left out. One that cannot go
            // back (an entry of its name is written already, or its bytes do not decode) is left out too, with a
            // warning, and counted.
            std::size_t kept = 0;
            if (entity.HasComponent<UnknownComponents>())
            {
                for (const UnknownComponent& unknown : entity.GetComponent<UnknownComponents>().components)
                {
                    const auto lose = [&](std::string_view why) {
                        Detail::Record({.severity = Ser::DiagnosticSeverity::Warning,
                                        .path = named,
                                        .message = std::format("entity `{}`: the kept `{}` {}; it is left out",
                                                               ids.at(id), unknown.typeName, why)},
                                       *m_state->logger, result.diagnostics);
                        ++result.componentsSkipped;
                        ++result.keptComponentsLost;
                    };
                    if (std::ranges::find(entries, unknown.typeName, &Ser::Member::key) != entries.end())
                    {
                        lose("has the name of a component the entity has now");
                        continue;
                    }
                    Ser::ReadTreeResult tree = Ser::DecodeTree(unknown.tree, named, *m_state->logger);
                    if (!tree.tree)
                    {
                        lose("is not one whole tree encoding");
                        continue;
                    }
                    entries.push_back({unknown.typeName, std::move(*tree.tree)});
                    ++kept;
                }
            }
            SortEntries(entries);
            Ser::Object components;
            components.members = std::move(entries);
            block.Add("components", std::move(components));
            blocks.Add(std::move(block));
            ++result.entitiesWritten;
            result.componentsWritten += reached + kept;
            result.componentsSkipped += entities.ComponentCountOf(id) - reached;
        }

        Ser::Object data;
        data.Add("resources", std::move(resources));
        data.Add("entities", std::move(blocks));
        Ser::Object file;
        file.Add("format", Detail::EnvelopeFormat);
        file.Add("type", Detail::SceneTypeName);
        file.Add("version", Detail::SceneVersion);
        file.Add("data", std::move(data));
        envelope = Ser::Value(std::move(file));
        result.status =
            result.nullReferences > 0 || result.keptComponentsLost > 0 ? SaveStatus::Partial : SaveStatus::Succeeded;
        return result;
    }
} // namespace Fenrir
