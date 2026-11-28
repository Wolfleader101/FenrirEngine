// The scene reader (ADR-0078, ADR-0079, ADR-0080): AssetStore::LoadScene, a scene envelope read into a new scene in two
// passes; AssetStore::LoadSceneInto, the same read merged into a scene that exists; and AssetStore::Instantiate, a
// scene or model file placed into a scene as one instance, which a block's `instance` expands through too.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
#include "AssetWatcher.hpp"
#include "Envelope.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ModelSpawner.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/HeldFields.hpp"
#include "FenrirSerialisation/ReadTree.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
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

        // The instance a block names, as `instance`: its source as written, and its asset path, which the chain of
        // files being read is checked against.
        struct BlockInstance
        {
            std::string source;
            std::string assetPath;
        };

        // Adds what `inner`, an instance's source read, found to `outer`, the read of the file holding the instance:
        // its counts and its diagnostics, already logged. The source's resources are never the host's to apply, so
        // they are no skipped resources of the host's.
        void AddInner(SceneLoadResult& outer, SceneLoadResult& inner)
        {
            outer.entities += inner.entities;
            outer.entitiesSkipped += inner.entitiesSkipped;
            outer.components += inner.components;
            outer.unknownComponentsKept += inner.unknownComponentsKept;
            outer.fieldsDropped += inner.fieldsDropped;
            outer.referencesEmptied += inner.referencesEmptied;
            Append(outer.diagnostics, std::move(inner.diagnostics));
        }

        // The one block of `data`, a scene envelope's, with no `parent`: the file's single root. Null when it has
        // several roots, or none.
        const Ser::Object* SingleRootBlock(const Ser::Value& data)
        {
            const Ser::Value* blocks = data.AsObject().Find("entities");
            if (blocks == nullptr || blocks->Kind() != Ser::ValueKind::Array)
            {
                return nullptr;
            }
            const Ser::Object* single = nullptr;
            for (const Ser::Element& element : blocks->AsArray().elements)
            {
                if (element.value.Kind() != Ser::ValueKind::Object ||
                    element.value.AsObject().Find("parent") != nullptr)
                {
                    continue;
                }
                if (single != nullptr)
                {
                    return nullptr;
                }
                single = &element.value.AsObject();
            }
            return single;
        }

        // A block's id as its diagnostics name it: `?` when it has none that parses.
        std::string Spelled(const std::optional<EntityId>& id)
        {
            return id ? SpellEntityId(*id) : std::string("?");
        }

        // Resets `value`, an instance of `type`, to the type's defaults in place: destroyed, then value-initialised.
        void ResetValue(const TypeDescription& type, void* value)
        {
            type.Destroy(value);
            type.Construct(value);
        }

        // Points each `entity` field of `value` naming an entity of `list` that `moved` has at the entity it maps to,
        // or at none when that is the null entity. Every field a file holds is walked: a list's elements, an
        // optional's value and a tagged union's live alternative.
        void RemapEntityFields(ValueRef value, void* list, const std::unordered_map<std::uint32_t, Entity>& moved)
        {
            switch (value.GetKind())
            {
            case Kind::Entity:
                if (const EntityValue held = value.Entity(); held.list == list)
                {
                    if (const auto found = moved.find(held.id); found != moved.end())
                    {
                        value.SetEntity(found->second.IsValid() ? EntityValue{found->second.GetId(), list}
                                                                : EntityValue{});
                    }
                }
                break;
            case Kind::Struct:
                for (const Ser::HeldField<ValueRef>& field : Ser::HeldFieldsOf(value))
                {
                    RemapEntityFields(field.value, list, moved);
                }
                break;
            case Kind::Array:
            case Kind::List:
                for (std::size_t index = 0; index < value.Size(); ++index)
                {
                    RemapEntityFields(value.Element(index), list, moved);
                }
                break;
            case Kind::Optional:
                if (value.HasValue())
                {
                    RemapEntityFields(value.Value(), list, moved);
                }
                break;
            default:
                break;
            }
        }

        // Remaps each `entity` field of `scene`'s described components and scene resources through `moved`, by
        // RemapEntityFields.
        void RemapSceneEntityFields(Scene& scene, const TypeRegistry& registry,
                                    const std::unordered_map<std::uint32_t, Entity>& moved)
        {
            EntityList& entities = scene.GetEntityList();
            void* const list = &entities;
            const std::vector<Detail::WrittenType> types = Detail::WrittenTypes(registry);
            entities.ForEachEntity([&](auto handle) {
                const auto entity = static_cast<std::uint32_t>(handle);
                for (const Detail::WrittenType& written : types)
                {
                    if (void* component = entities.Get(written.component, entity))
                    {
                        RemapEntityFields(ValueRef(component, ValueType{.kind = Kind::Struct, .type = written.type}),
                                          list, moved);
                    }
                }
            });
            for (const TypeDescription* type : registry.Types(Role::SceneResource))
            {
                if (void* resource = scene.FindResource(registry, type->name))
                {
                    RemapEntityFields(ValueRef(resource, ValueType{.kind = Kind::Struct, .type = type}), list, moved);
                }
            }
        }

        // Whether `entity`, inside an instance and holding the id `value`, is its source's: one the read of the
        // nearest instance above it gave out, as `scopes` records it. An entity there that no recorded read gave its
        // id is a host's.
        bool SourceEntity(const Detail::InstanceScopes* scopes, const Entity& entity, std::uint64_t value)
        {
            if (scopes == nullptr)
            {
                return false;
            }
            for (Entity above = entity.GetComponent<Relationship>().parent; above.IsValid();
                 above = above.GetComponent<Relationship>().parent)
            {
                if (above.HasComponent<Instance>())
                {
                    const auto scope = scopes->find(above.GetId());
                    return scope != scopes->end() && scope->second.ids.contains(value);
                }
            }
            return false;
        }

        // Records `roots`, a read's top-level entities in its file's order, in `scene`'s root order: after those it
        // lists already, or, for a patch, before them, since the patch restores its file's order. An entity destroyed
        // or given a parent since it was listed goes.
        void OrderRoots(Scene& scene, const std::vector<Entity>& roots, bool patch)
        {
            Detail::SceneRootOrder* order = scene.GetResource<Detail::SceneRootOrder>();
            if (order == nullptr)
            {
                order = &scene.AddResource<Detail::SceneRootOrder>();
            }
            std::vector<Entity> before = std::move(order->roots);
            const std::vector<Entity>& first = patch ? roots : before;
            const std::vector<Entity>& then = patch ? before : roots;
            EntityList& entities = scene.GetEntityList();
            std::unordered_set<std::uint32_t> listed;
            std::vector<Entity> ordered;
            for (const std::vector<Entity>* part : {&first, &then})
            {
                for (const Entity& top : *part)
                {
                    if (Detail::LiveIn(entities, top) && !top.GetComponent<Relationship>().parent.IsValid() &&
                        listed.insert(top.GetId()).second)
                    {
                        ordered.push_back(top);
                    }
                }
            }
            order->roots = std::move(ordered);
        }

        // One load's work: the scene being filled, where its types are found, its file's path for diagnostics, how
        // its asset handle fields are read, and the result being filled; and for a merge, the entity the file's roots
        // go under (none for the top) and which door the read serves. An instance block's source is expanded through
        // `expand`, unless it is on `reading`, the chain of scene files being read, its path resolved against
        // `assetRoot`.
        struct SceneFiller
        {
            Scene& scene;
            const TypeRegistry& registry;
            std::string_view path;
            const std::function<Detail::ReferenceRead(std::string_view, const FieldInfo&)>& readReference;
            ILogger& logger;
            SceneLoadResult& result;
            Entity parent;
            Detail::SceneRead read = Detail::SceneRead::Load;
            const std::filesystem::path& assetRoot;
            const std::vector<std::string>& reading;
            std::function<InstanceResult(std::string_view)> expand;

            // Each block's entity by its id's value: the first block holding an id.
            std::unordered_map<std::uint64_t, std::uint32_t> ids;
            // The ids the scene's entities hold, from before the load and as it gives them: a block's id one of them
            // has already is re-minted, so a merge never repeats an id in its scene.
            std::unordered_set<std::uint64_t> taken;
            // For a patch (ADR-0081 §3): the scene's entities outside any instance, and those the file added inside
            // one, by the value of the id each holds, which a block of that id fills rather than adding one; the
            // entities matched so, which the patch keeps; and the ids the file held when it last filled the scene,
            // whose entities the file no longer has go.
            std::unordered_map<std::uint64_t, std::uint32_t> live;
            std::unordered_set<std::uint32_t> matched;
            const std::unordered_set<std::uint64_t>* priorIds = nullptr;
            // For an instance re-patch (ADR-0081 §4): the instance's root, which the source's single root block fills
            // and which is never moved or destroyed.
            Entity scopeRoot;
            // For a patch or a re-patch: reads a matched instance block's instance again from its source, its diff
            // then applied on top; whether the source read. Null for every other read.
            std::function<bool(Entity)> repatch;
            // The unknown component types already warned of in this load.
            std::unordered_set<std::string> unknownWarned;
            // Every described component type a block can list, which a re-patch removes from a matched entity when
            // its block does not; found the first time it is asked for.
            std::optional<std::vector<Detail::WrittenType>> writtenTypes;

            // The described component types a block can list.
            const std::vector<Detail::WrittenType>& Written()
            {
                if (!writtenTypes)
                {
                    writtenTypes = Detail::WrittenTypes(registry);
                }
                return *writtenTypes;
            }

            // Whether this read patches entities the scene has: a scene patch, or an instance re-patch.
            [[nodiscard]] bool Patching() const
            {
                return read == Detail::SceneRead::Patch || read == Detail::SceneRead::Repatch;
            }

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
                    // `<id>` names a block; `<id>/<source id>...` an entity inside the instance that block is.
                    const std::size_t slash = id.find('/');
                    const std::optional<EntityId> parsed = ParseEntityId(id.substr(0, slash));
                    if (!parsed)
                    {
                        return std::nullopt;
                    }
                    const auto found = ids.find(parsed->value);
                    if (found == ids.end())
                    {
                        return std::nullopt;
                    }
                    if (slash == std::string_view::npos)
                    {
                        return EntityValue{found->second, &entities};
                    }
                    const Entity inside =
                        Detail::FollowSlashPath(entities.GetEntity(found->second), id.substr(slash + 1));
                    if (!inside.IsValid())
                    {
                        return std::nullopt;
                    }
                    return EntityValue{inside.GetId(), &entities};
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
            // a `parent` may name. A patch's `matchedEntity` is the scene's already, holding the id: each component the
            // block writes is reset and read again, a Name or Transform it does not write goes (for a re-patch, any
            // described component it does not write), the rest are left, and it is taken from its parent first, so
            // appending it restores its parent and its order.
            void FillBlock(Ser::Object& block, const Ser::Value& at, std::uint32_t entity, std::optional<EntityId> id,
                           bool holdsId, bool matchedEntity, const std::unordered_set<std::uint32_t>& filled)
            {
                EntityList& entities = scene.GetEntityList();
                if (!matchedEntity)
                {
                    entities.CompleteEntity(entity);
                }
                Entity handle = entities.GetEntity(entity);
                ++result.entities;
                const std::string spelled = Spelled(id);
                if (holdsId && !matchedEntity)
                {
                    GiveId(handle, *id);
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
                        FillComponent(member, *components, entity, spelled, unknown, matchedEntity);
                    }
                }
                const auto lists = [&](std::string_view type) {
                    return components != nullptr && components->AsObject().Find(type) != nullptr;
                };
                // The block lists every described component its entity has, Name and Transform at their defaults as
                // `{}`, so one it leaves out the entity had not (ADR-0078 §5). A new entity loses CompleteEntity's,
                // and a patch's matched one loses the Name or Transform it had, as a load gives it neither; the rest
                // of a patched entity's components are the scene's, which the file does not write (ADR-0081 §3).
                if (matchedEntity && read == Detail::SceneRead::Repatch)
                {
                    // A source entity's components are all its source's, but what the host's overrides add, and
                    // those are reapplied on top (ADR-0081 §4): one the source no longer gives it goes.
                    for (const Detail::WrittenType& written : Written())
                    {
                        if (!lists(written.type->name) && entities.Has(written.component, entity))
                        {
                            entities.Remove(written.component, entity);
                        }
                    }
                }
                else
                {
                    if (!lists(Detail::NameType) && handle.HasComponent<Name>())
                    {
                        handle.RemoveComponent<Name>();
                    }
                    if (!lists(Detail::TransformType) && handle.HasComponent<Transform>())
                    {
                        handle.RemoveComponent<Transform>();
                    }
                }
                if (matchedEntity)
                {
                    KeepUnknown(handle, std::move(unknown));
                    if (handle == scopeRoot)
                    {
                        // The instance's root stays where its host put it.
                        return;
                    }
                    Detach(handle);
                }
                else if (!unknown.components.empty())
                {
                    handle.AddComponent<UnknownComponents>(std::move(unknown));
                }
                Attach(block, at, handle, spelled, filled);
            }

            // Takes `entity` from its parent's children, when it has a parent.
            static void Detach(Entity entity)
            {
                const Entity above = entity.GetComponent<Relationship>().parent;
                if (above.IsValid())
                {
                    above.RemoveChild(entity);
                }
            }

            // Puts each component `unknown` keeps on `entity`, in place of one of its type name the entity kept.
            static void KeepUnknown(Entity entity, UnknownComponents unknown)
            {
                if (unknown.components.empty())
                {
                    return;
                }
                if (!entity.HasComponent<UnknownComponents>())
                {
                    entity.AddComponent<UnknownComponents>();
                }
                std::vector<UnknownComponent>& kept = entity.GetComponent<UnknownComponents>().components;
                for (UnknownComponent& added : unknown.components)
                {
                    std::erase_if(kept, [&](const UnknownComponent& was) { return was.typeName == added.typeName; });
                    kept.push_back(std::move(added));
                }
            }

            // Gives `entity` its block's id, `id`: as it is, unless an entity of the scene from before a merge has it,
            // when it is re-minted. The id map keeps the file's id, so the file's `entity` fields and `parent`s still
            // name the block; only the entity's own id is fresh. An instance read takes no ids, so it keeps them all.
            void GiveId(Entity entity, EntityId id)
            {
                EntityId held = id;
                while (taken.contains(held.value) || (held.value != id.value && ids.contains(held.value)))
                {
                    held = MintEntityId();
                }
                taken.insert(held.value);
                if (entity.HasComponent<EntityId>())
                {
                    entity.GetComponent<EntityId>() = held;
                }
                else
                {
                    entity.AddComponent<EntityId>(held);
                }
            }

            // Fills the root of an instance block, `root`, which the first pass expanded: gives it the block's id,
            // applies the block's diff when the source `expanded`, and appends it to its parent.
            void FillInstance(Ser::Object& block, const Ser::Value& at, Entity root, std::optional<EntityId> id,
                              bool holdsId, bool expanded, const std::unordered_set<std::uint32_t>& filled)
            {
                if (holdsId)
                {
                    GiveId(root, *id);
                }
                const std::string spelled = Spelled(id);
                if (expanded)
                {
                    ApplyDiff(block, at, root, spelled);
                }
                Attach(block, at, root, spelled, filled);
            }

            // The entity `key`, a slash path from the instance `root` tops, names, or the null entity with a warning at
            // `node` naming `what` (the diff's member holding the key) when it names none.
            Entity Keyed(Entity root, std::string_view key, const Ser::Value* node, const Ser::Value& at,
                         const std::string& spelled, std::string_view what)
            {
                const Entity found = Detail::FollowSlashPath(root, key);
                if (!found.IsValid() || found == root)
                {
                    Report(Ser::DiagnosticSeverity::Warning, node, at,
                           std::format("entity `{}`: the {} `{}` names no entity of the instance's source; it is "
                                       "ignored",
                                       spelled, what, key));
                    return {};
                }
                return found;
            }

            // Applies an instance block's diff to `root`, its expanded source (ADR-0080 §4): the source entities in
            // `removedEntities` are destroyed with what is below them; then the root takes the block's `components`,
            // merged on top of its own, and loses those `removed` names; then each entry of `overrides` does the same
            // to the source entity its key names.
            void ApplyDiff(Ser::Object& block, const Ser::Value& at, Entity root, const std::string& spelled)
            {
                if (Ser::Value* removed = block.Find("removedEntities"))
                {
                    if (removed->Kind() != Ser::ValueKind::Array)
                    {
                        Report(Ser::DiagnosticSeverity::Error, removed, at,
                               std::format("entity `{}`: expected `removedEntities`, an array of the slash paths of "
                                           "source entities, found {}; no entity is removed",
                                           spelled, Detail::Found(removed)));
                    }
                    else
                    {
                        for (const Ser::Element& element : removed->AsArray().elements)
                        {
                            const std::string key = element.value.Kind() == Ser::ValueKind::String
                                                        ? element.value.AsString()
                                                        : Detail::Found(&element.value);
                            if (const Entity gone =
                                    Keyed(root, key, &element.value, *removed, spelled, "removed entity");
                                gone.IsValid())
                            {
                                Detail::DestroySubtree(scene.GetEntityList(), gone);
                            }
                        }
                    }
                }
                ApplyEntityDiff(block, at, root, spelled);

                Ser::Value* overrides = block.Find("overrides");
                if (overrides == nullptr)
                {
                    return;
                }
                if (overrides->Kind() != Ser::ValueKind::Object)
                {
                    Report(Ser::DiagnosticSeverity::Error, overrides, at,
                           std::format("entity `{}`: expected `overrides`, an object of diffs by the slash paths of "
                                       "source entities, found {}; none is applied",
                                       spelled, Detail::Found(overrides)));
                    return;
                }
                for (Ser::Member& member : overrides->AsObject().members)
                {
                    const Entity target = Keyed(root, member.key, &member.value, *overrides, spelled, "override");
                    const std::string named = std::format("{}/{}", spelled, member.key);
                    if (!target.IsValid())
                    {
                        continue;
                    }
                    if (member.value.Kind() != Ser::ValueKind::Object)
                    {
                        Report(Ser::DiagnosticSeverity::Error, &member.value, *overrides,
                               std::format("entity `{}`: expected an override, an object of `components` and "
                                           "`removed`, found {}; it is ignored",
                                           named, Detail::Found(&member.value)));
                        continue;
                    }
                    ApplyEntityDiff(member.value.AsObject(), member.value, target, named);
                }
            }

            // Merges the entries of `diff`'s `components` into `entity`'s components, field by field over those it
            // has and whole for those it lacks, then removes each component `removed` names.
            void ApplyEntityDiff(Ser::Object& diff, const Ser::Value& at, Entity entity, const std::string& spelled)
            {
                Ser::Value* components = diff.Find("components");
                if (components != nullptr && components->Kind() != Ser::ValueKind::Object)
                {
                    Report(Ser::DiagnosticSeverity::Error, components, at,
                           std::format("entity `{}`: expected `components`, an object of entries by type name, found "
                                       "{}; none is applied",
                                       spelled, Detail::Found(components)));
                    components = nullptr;
                }
                if (components != nullptr)
                {
                    UnknownComponents unknown;
                    for (Ser::Member& member : components->AsObject().members)
                    {
                        // A field missing from the entry keeps the value the source gave it.
                        FillComponent(member, *components, entity.GetId(), spelled, unknown, false);
                    }
                    KeepUnknown(entity, std::move(unknown));
                }

                Ser::Value* removed = diff.Find("removed");
                if (removed == nullptr)
                {
                    return;
                }
                if (removed->Kind() != Ser::ValueKind::Array)
                {
                    Report(Ser::DiagnosticSeverity::Error, removed, at,
                           std::format("entity `{}`: expected `removed`, an array of type names, found {}; no "
                                       "component is removed",
                                       spelled, Detail::Found(removed)));
                    return;
                }
                for (const Ser::Element& element : removed->AsArray().elements)
                {
                    RemoveNamed(entity, element.value, *removed, spelled);
                }
            }

            // Removes from `entity` the component `name` names, a described one or one a load kept opaque; a name of
            // neither is warned of.
            void RemoveNamed(Entity entity, const Ser::Value& name, const Ser::Value& at, const std::string& spelled)
            {
                if (name.Kind() == Ser::ValueKind::String &&
                    std::ranges::find(Detail::SpelledApart, name.AsString()) == std::end(Detail::SpelledApart))
                {
                    const TypeDescription* type = registry.Find(name.AsString());
                    const std::optional<ComponentType> component = type != nullptr && type->role == Role::Component
                                                                       ? ComponentType::ForName(registry, type->name)
                                                                       : std::nullopt;
                    EntityList& entities = scene.GetEntityList();
                    if (component && entities.Get(*component, entity.GetId()) != nullptr)
                    {
                        entities.Remove(*component, entity.GetId());
                        return;
                    }
                    if (entity.HasComponent<UnknownComponents>())
                    {
                        std::vector<UnknownComponent>& kept = entity.GetComponent<UnknownComponents>().components;
                        if (std::erase_if(
                                kept, [&](const UnknownComponent& was) { return was.typeName == name.AsString(); }) > 0)
                        {
                            return;
                        }
                    }
                }
                Report(Ser::DiagnosticSeverity::Warning, &name, at,
                       std::format("entity `{}`: `removed` names {}, which is no component the source gives it; it "
                                   "is ignored",
                                   spelled, Detail::Found(&name)));
            }

            // Appends `handle`, the entity of `block`, to the block before it that its `parent` names, or to the
            // entity inside such a block's instance that a slash path names (`"<id>/<source id>"`: an entity the host
            // added to the instance), or makes it a root of the file when it has none, or one naming no such entity.
            void Attach(const Ser::Object& block, const Ser::Value& at, Entity handle, const std::string& spelled,
                        const std::unordered_set<std::uint32_t>& filled)
            {
                EntityList& entities = scene.GetEntityList();
                const Ser::Value* parent = block.Find("parent");
                if (parent == nullptr)
                {
                    AddRoot(handle);
                    return;
                }
                const std::string_view text = parent->Kind() == Ser::ValueKind::String
                                                  ? std::string_view(parent->AsString())
                                                  : std::string_view();
                const std::size_t slash = text.find('/');
                const std::optional<EntityId> parentId = ParseEntityId(text.substr(0, slash));
                const auto found = parentId ? ids.find(parentId->value) : ids.end();
                Entity above;
                if (found != ids.end() && filled.contains(found->second))
                {
                    above = entities.GetEntity(found->second);
                    if (slash != std::string_view::npos)
                    {
                        above = Detail::FollowSlashPath(above, text.substr(slash + 1));
                    }
                }
                if (!above.IsValid())
                {
                    Report(Ser::DiagnosticSeverity::Error, parent, at,
                           std::format("entity `{}`: expected `parent`, the id of a block before it or the slash path "
                                       "of an entity inside one's instance, found {}; the entity loads at the top",
                                       spelled, Detail::Found(parent)));
                    AddRoot(handle);
                    return;
                }
                above.AddChild(handle);
            }

            // Records `root`, an entity of no block's, as a root of the file, appended under the merge's parent when
            // there is one, so the file's roots follow its children in file order.
            void AddRoot(Entity root)
            {
                if (parent.IsValid())
                {
                    parent.AddChild(root);
                }
                result.roots.push_back(root);
            }

            // Adds the component `member` holds to `entity`, or keeps it in `unknown` when the registry lacks its
            // type as a component. With `reset`, a component the entity has is reset to its type's defaults first, so
            // it holds what the entry writes and nothing from before: a patch rewrites it.
            void FillComponent(Ser::Member& member, const Ser::Value& at, std::uint32_t entity,
                               const std::string& spelled, UnknownComponents& unknown, bool reset)
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
                const bool had = entities.Has(*component, entity);
                entities.Add(*component, entity);
                void* value = entities.Get(*component, entity);
                if (reset && had)
                {
                    // In place, so the component stays the one the scene's systems and watches know.
                    ResetValue(*type, value);
                }
                Fill(member.value, *type, version, value, std::format("entity `{}`, `{}`: ", spelled, type->name));
                ++result.components;
            }

            // Adds or fills each scene resource `resources` holds, after every entity, so an `entity` field in one
            // names an entity of the file (ADR-0078 §7). A patch rewrites one the scene has, reset first, in place. A
            // merge or an instance applies none: the target scene's resources stand, and each entry is counted as
            // skipped, with no diagnostic (ADR-0079 §2).
            void FillResources(Ser::Value& resources)
            {
                if (read != Detail::SceneRead::Load && read != Detail::SceneRead::Patch)
                {
                    result.resourcesSkipped += resources.AsObject().members.size();
                    return;
                }
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
                        else if (read == Detail::SceneRead::Patch)
                        {
                            ResetValue(*type, resource);
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

            // The instance `block` names, or none when it has no `instance`, or one that is not a string (an error, and
            // the block loads as an ordinary one). A source outside the asset root keeps its text as its asset path,
            // and fails when it is expanded.
            std::optional<BlockInstance> InstanceNamed(const Ser::Object& block, const Ser::Value& at,
                                                       const std::optional<EntityId>& id)
            {
                const Ser::Value* instance = block.Find("instance");
                if (instance == nullptr)
                {
                    return std::nullopt;
                }
                if (instance->Kind() != Ser::ValueKind::String)
                {
                    Report(Ser::DiagnosticSeverity::Error, instance, at,
                           std::format("entity `{}`: expected `instance`, the asset path of a scene or model file, "
                                       "found {}; the entity loads as an ordinary one",
                                       Spelled(id), Detail::Found(instance)));
                    return std::nullopt;
                }
                BlockInstance named{instance->AsString(), {}};
                std::string fileKey;
                if (Detail::WholeFileLoadProblem(assetRoot, named.source, named.assetPath, fileKey))
                {
                    named.assetPath = named.source;
                }
                return named;
            }

            // Whether the scene file `assetPath` is being read now: this file, or one instancing it through a chain.
            [[nodiscard]] bool OnReadChain(const std::string& assetPath) const
            {
                return std::ranges::find(reading, assetPath) != reading.end();
            }

            // Expands the source of the instance block `block` at the top of the scene, adding what its read found to
            // this load's, and gives its root's entity, `expanded` when the source loaded. A source that does not load
            // leaves the root alone, marked with `Instance`, with an error, and its diff unread: a save writes the
            // block back naming its source alone.
            std::uint32_t Expand(const BlockInstance& instance, const Ser::Object& block, const Ser::Value& at,
                                 const std::optional<EntityId>& id, bool& expanded)
            {
                InstanceResult inner = expand(instance.source);
                AddInner(result, inner.load);
                expanded = inner.root.IsValid();
                if (expanded)
                {
                    return inner.root.GetId();
                }
                Report(Ser::DiagnosticSeverity::Error, block.Find("instance"), at,
                       std::format("entity `{}`: the instance's source `{}` did not load; the entity loads as the "
                                   "instance's root alone, so a save keeps the block",
                                   Spelled(id), instance.source));
                Entity root = scene.GetEntityList().CreateEntity();
                ++result.entities;
                if (const std::optional<Instance> mark = Detail::InstanceOf(instance.source))
                {
                    root.AddComponent<Instance>(*mark);
                }
                return root.GetId();
            }

            // For a patch: the entity of the scene a block of id `id` fills, when one `live` has holds it and no
            // earlier block has the id. It is an instance's root of the source `instance` names for an instance
            // block, and no instance's root for any other; one that is not is no match, so the block adds its entity
            // afresh and the one the scene had goes.
            [[nodiscard]] std::optional<std::uint32_t> Matching(const std::optional<EntityId>& id,
                                                                const std::optional<BlockInstance>& instance)
            {
                if (!Patching() || !id || ids.contains(id->value))
                {
                    return std::nullopt;
                }
                const auto found = live.find(id->value);
                if (found == live.end())
                {
                    return std::nullopt;
                }
                const Entity entity = scene.GetEntityList().GetEntity(found->second);
                if (entity == scopeRoot)
                {
                    // A re-patch's root is the instance's, marked so, and filled by the source's ordinary root block.
                    return instance ? std::nullopt : std::optional<std::uint32_t>(found->second);
                }
                const bool instanced = entity.HasComponent<Instance>();
                if (instanced != instance.has_value() ||
                    (instanced && std::string_view(entity.GetComponent<Instance>().source) != instance->source))
                {
                    return std::nullopt;
                }
                return found->second;
            }

            // For a patch: destroys, with what is below it, each entity of the scene holding an id the file held when
            // it last filled the scene that no block matched. It runs after every block is placed, so an entity the
            // file moved from under one that goes is kept.
            void DestroyGone()
            {
                if (!Patching() || priorIds == nullptr)
                {
                    return;
                }
                EntityList& entities = scene.GetEntityList();
                for (const auto& [value, entity] : live)
                {
                    const Entity gone = entities.GetEntity(entity);
                    if (priorIds->contains(value) && !matched.contains(entity) && gone != scopeRoot &&
                        Detail::LiveIn(entities, gone))
                    {
                        Detail::DestroySubtree(entities, gone);
                    }
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

                // Pass one: an entity for every block, and the id map, so a field or a `parent` names any block. An
                // instance block's entity is its expanded source's root, so a slash path names an entity inside it.
                struct Reserved
                {
                    Ser::Object* block = nullptr;
                    const Ser::Value* at = nullptr;
                    std::uint32_t entity = 0;
                    // The block's id, when it parsed; the entity holds it only when no earlier block has it.
                    std::optional<EntityId> id;
                    bool holdsId = false;
                    // Whether the entity is an instance's root, made complete already, and whether its source loaded.
                    bool instance = false;
                    bool expanded = false;
                    // For a patch: whether the entity is one the scene had, holding the block's id.
                    bool matched = false;
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
                        const std::optional<BlockInstance> instance = InstanceNamed(block, element.value, id);
                        if (instance && OnReadChain(instance->assetPath))
                        {
                            Report(Ser::DiagnosticSeverity::Error, block.Find("instance"), element.value,
                                   std::format("entity `{}`: the instance's source `{}` is {}, so it would instance "
                                               "itself; the block is left out",
                                               Spelled(id), instance->source,
                                               instance->assetPath == reading.back() ? "this file"
                                                                                     : "a file instancing this one"));
                            ++result.entitiesSkipped;
                            continue;
                        }
                        if (Patching() && (!id || ids.contains(id->value)))
                        {
                            // A block with no id of its own matches nothing: the entity the scene's load made for it
                            // stays as it is, as an entity with no id does, rather than another being added each patch.
                            ++result.entitiesSkipped;
                            continue;
                        }
                        bool expanded = false;
                        const std::optional<std::uint32_t> found = Matching(id, instance);
                        const std::uint32_t entity = found      ? *found
                                                     : instance ? Expand(*instance, block, element.value, id, expanded)
                                                                : entities.ReserveEntity();
                        if (found)
                        {
                            matched.insert(*found);
                        }
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
                        reserved.push_back({&block, &element.value, entity, id, holdsId, instance.has_value(), expanded,
                                            found.has_value()});
                    }
                }

                // Pass two, in file order: each entity completed and filled, then appended to its parent.
                std::unordered_set<std::uint32_t> filled;
                for (const Reserved& entry : reserved)
                {
                    if (entry.matched && entry.instance)
                    {
                        // The instance is read again from its source, and its diff applied on top, so an edit to
                        // either shows (ADR-0081 §4); then its parent and order are restored. A source that does not
                        // read leaves it as it was.
                        Entity root = entities.GetEntity(entry.entity);
                        ++result.entities;
                        if (repatch)
                        {
                            // Over the source read again, or, when it does not read, over the instance as it was, so
                            // an edit to the diff shows either way.
                            (void)repatch(root);
                            ApplyDiff(*entry.block, *entry.at, root, Spelled(entry.id));
                        }
                        Detach(root);
                        Attach(*entry.block, *entry.at, root, Spelled(entry.id), filled);
                    }
                    else if (entry.instance)
                    {
                        FillInstance(*entry.block, *entry.at, entities.GetEntity(entry.entity), entry.id, entry.holdsId,
                                     entry.expanded, filled);
                    }
                    else
                    {
                        FillBlock(*entry.block, *entry.at, entry.entity, entry.id, entry.holdsId, entry.matched,
                                  filled);
                    }
                    filled.insert(entry.entity);
                }
                DestroyGone();

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
    } // namespace

    const Ser::Value* AssetStore::SceneSourceTree(const std::string& assetPath, const std::string& fileKey,
                                                  std::string_view what)
    {
        Detail::SourceTrees& trees = *m_state->sourceTrees;
        if (const auto found = trees.find(fileKey); found != trees.end())
        {
            return found->second ? &*found->second : nullptr;
        }
        std::optional<Ser::Value>& kept = trees[fileKey];
        ILogger& logger = *m_state->logger;
        const std::optional<std::vector<std::byte>> bytes = m_state->source->Read(assetPath);
        if (!bytes)
        {
            logger.Error("Could not {} '{}': the file could not be read; they are left as they were", what, assetPath);
            return nullptr;
        }
        Ser::ReadTreeResult tree = Ser::ReadTree(*bytes, fileKey, logger);
        if (!tree.tree)
        {
            // ReadTree logged its own located diagnostics.
            logger.Error("Could not {} '{}': the file is neither JSON5 nor the tree encoding; they are left as they "
                         "were",
                         what, assetPath);
            return nullptr;
        }
        Ser::Value* data = nullptr;
        if (const std::optional<Detail::SceneEnvelopeProblem> problem = Detail::CheckSceneEnvelope(*tree.tree, data))
        {
            logger.Error("Could not {} '{}': {}; they are left as they were", what, assetPath,
                         Ser::FormatDiagnostic(Detail::LocatedAt(Ser::DiagnosticSeverity::Error, fileKey, problem->node,
                                                                 *tree.tree, problem->message)));
            return nullptr;
        }
        kept = std::move(*tree.tree);
        return &*kept;
    }

    bool AssetStore::RepatchInstance(Scene& scene, Entity root)
    {
        Detail::InstanceScopes* const scopes = m_state->scopeSink;
        if (scopes == nullptr || !root.HasComponent<Instance>())
        {
            return false;
        }
        const Detail::SourceTreesRun run(m_state->sourceTrees);
        // Unordered map elements stay put as nested expansions add theirs, so the scope can be held throughout.
        Detail::InstanceScope& scope = (*scopes)[root.GetId()];
        if (!scope.source.empty() && scope.source != std::string_view(root.GetComponent<Instance>().source))
        {
            // A scope left by an instance since destroyed, whose entity's slot another instance now holds.
            scope = {};
        }
        if (scope.source.empty())
        {
            // An instance no read recorded, as one whose source did not load: its source from its Instance, and no
            // entity below it the source's yet.
            const std::string source(root.GetComponent<Instance>().source);
            if (Detail::WholeFileLoadProblem(m_state->root, source, scope.source, scope.sourceKey))
            {
                scopes->erase(root.GetId());
                return false;
            }
            scope.model = !scope.source.ends_with(".scene.json5");
        }
        EntityList& entities = scene.GetEntityList();

        if (!scope.model)
        {
            const Ser::Value* tree = SceneSourceTree(scope.source, scope.sourceKey, "re-patch the instances of");
            if (tree == nullptr)
            {
                return false;
            }
            Ser::Value copy = *tree;
            std::unordered_set<std::uint64_t> ids = scope.ids;
            std::optional<std::uint64_t> rootId = scope.rootId;
            const SceneLoadResult read = ReadSceneTree(
                copy, scope.source, scope.source, scope.sourceKey, [&]() -> Scene& { return scene; }, root,
                Detail::SceneRead::Repatch, {}, &ids, &rootId);
            if (read.status == LoadStatus::Failed)
            {
                return false;
            }
            scope.ids = std::move(ids);
            scope.rootId = rootId;
            return true;
        }

        // A model re-spawns (ADR-0081 §4): its import was replaced in place, so the handle the scene holds names the
        // new one. The spawn hangs a fresh root under the instance's, whose entities take the old ones' places.
        const LoadResult<Model> model = Load<Model>(scope.source, AssetOwner(scene));
        if (!model.Ok())
        {
            return false;
        }
        const std::uint32_t revision =
            m_state->Find(Detail::AssetKind::Model, {model.handle.index, model.handle.generation})->revision;
        if (revision == scope.revision && !scope.ids.empty())
        {
            // The import is the one spawned, so the instance's entities stand, and keep their handles; only its diff,
            // the caller's, is applied again.
            return true;
        }
        // The model's entities are those in the root's own scope: an instance below one of them (another model
        // attached at run time, whose ids are numbers too) is in its own, and stays, as a run-time child does.
        std::vector<Entity> old;
        std::unordered_set<std::uint32_t> oldSet;
        Detail::ForEachInInstance(root, [&](const Entity& entity) {
            if (entity.HasComponent<EntityId>() && scope.ids.contains(entity.GetComponent<EntityId>().value) &&
                oldSet.insert(entity.GetId()).second)
            {
                old.push_back(entity);
            }
        });
        const SpawnedNodes spawned = m_state->modelSpawner != nullptr
                                         ? m_state->modelSpawner->Spawn(scene, model.handle, root)
                                         : SpawnNodes(scene, model.handle, root);
        if (!spawned.root.IsValid())
        {
            m_state->logger->Error("Could not re-spawn the instances of '{}': the model spawned nothing; they are left "
                                   "as they were",
                                   scope.source);
            return false;
        }
        const Entity fresh = spawned.root;
        std::unordered_map<std::uint64_t, Entity> made;
        std::unordered_set<std::uint64_t> madeIds;
        Detail::ForEachInInstance(fresh, [&](const Entity& entity) {
            if (entity.HasComponent<EntityId>())
            {
                made.emplace(entity.GetComponent<EntityId>().value, entity);
                madeIds.insert(entity.GetComponent<EntityId>().value);
            }
        });
        // Each old entity's new one, by its id, or none: the null entity.
        std::unordered_map<std::uint32_t, Entity> moved;
        for (const Entity& was : old)
        {
            const auto to = made.find(was.GetComponent<EntityId>().value);
            moved.emplace(was.GetId(), to != made.end() ? to->second : Entity{});
        }
        // What hangs from an old entity that is not the model's (a run-time child, or one a host added) moves to the
        // new entity of its id, or to the root when the model has none any more.
        for (const Entity& was : old)
        {
            std::vector<Entity> kept;
            was.ForEachChild([&](const Entity& child) {
                if (!oldSet.contains(child.GetId()))
                {
                    kept.push_back(child);
                }
            });
            const auto to = made.find(was.GetComponent<EntityId>().value);
            Entity into = to != made.end() ? to->second : root;
            for (const Entity& child : kept)
            {
                was.RemoveChild(child);
                into.AddChild(child);
            }
        }
        for (const Entity& was : old)
        {
            if (Detail::LiveIn(entities, was))
            {
                Detail::DestroySubtree(entities, was);
            }
        }
        std::vector<Entity> top;
        fresh.ForEachChild([&](const Entity& child) { top.push_back(child); });
        for (const Entity& child : top)
        {
            fresh.RemoveChild(child);
            root.AddChild(child);
        }
        Detail::DestroySubtree(entities, fresh);
        scope.ids = std::move(madeIds);
        scope.revision = revision;

        // An `entity` field naming an old entity (a host's, into the model) names the new entity of its id now, or
        // none when the model has none any more, as a load of the host would resolve it.
        RemapSceneEntityFields(scene, *m_state->types, moved);
        return true;
    }

    void AssetStore::RepatchHostInstances(Scene& scene, Ser::Value& tree, const std::string& assetPath,
                                          const std::string& fileKey, const std::vector<std::vector<Entity>>& chains)
    {
        Ser::Value* data = nullptr;
        if (Detail::CheckSceneEnvelope(tree, data))
        {
            return;
        }
        SceneLoadResult result;
        const std::uint32_t owner = OwnerOf(AssetOwner(scene));
        const std::function<Detail::ReferenceRead(std::string_view, const FieldInfo&)> readReference =
            [&](std::string_view reference, const FieldInfo& field) {
                return ReadFileReference(assetPath, owner, reference, field);
            };
        // The host's diffs are read as its patch reads them, an `entity` field naming the host's entity of that id.
        SceneFiller filler{scene,
                           *m_state->types,
                           fileKey,
                           readReference,
                           *m_state->logger,
                           result,
                           {},
                           Detail::SceneRead::Patch,
                           m_state->root,
                           m_state->scenesReading,
                           [&](std::string_view source) { return ExpandInstance(scene, source); }};
        // The host's blocks by id, the first of each.
        std::unordered_map<std::uint64_t, Ser::Element*> blocks;
        if (Ser::Value* listed = data->AsObject().Find("entities");
            listed != nullptr && listed->Kind() == Ser::ValueKind::Array)
        {
            for (Ser::Element& element : listed->AsArray().elements)
            {
                if (element.value.Kind() != Ser::ValueKind::Object)
                {
                    continue;
                }
                const Ser::Value* id = element.value.AsObject().Find("id");
                const std::optional<EntityId> parsed = id != nullptr && id->Kind() == Ser::ValueKind::String
                                                           ? ParseEntityId(id->AsString())
                                                           : std::nullopt;
                if (parsed)
                {
                    blocks.emplace(parsed->value, &element);
                }
            }
        }
        // Each host entity by its id: those at the top, and those the host added inside an instance, whose ids are
        // its blocks'.
        EntityList& entities = scene.GetEntityList();
        entities.ForEachEntity([&](auto handle) {
            const Entity entity = entities.GetEntity(static_cast<std::uint32_t>(handle));
            if (entity.HasComponent<EntityId>() &&
                (!Detail::InsideInstance(entity) || blocks.contains(entity.GetComponent<EntityId>().value)))
            {
                filler.ids.emplace(entity.GetComponent<EntityId>().value, entity.GetId());
            }
        });
        // The host block writing `entity` as an instance of its source, or null: a host's, not a source's.
        const auto hostBlock = [&](const Entity& entity) -> Ser::Element* {
            if (!Detail::LiveIn(entities, entity) || !entity.HasComponent<EntityId>() ||
                !entity.HasComponent<Instance>())
            {
                return nullptr;
            }
            const auto found = blocks.find(entity.GetComponent<EntityId>().value);
            const Ser::Value* instance =
                found != blocks.end() ? found->second->value.AsObject().Find("instance") : nullptr;
            if (instance == nullptr || instance->Kind() != Ser::ValueKind::String ||
                instance->AsString() != std::string_view(entity.GetComponent<Instance>().source))
            {
                return nullptr;
            }
            return found->second;
        };

        // Each chain runs from an instance of the source out to the top-level instance holding it; the instance
        // re-patched is the nearest the host writes, so one the host added inside another is re-patched itself. One
        // the host no longer writes as an instance of this source is the host's patch's to change.
        std::unordered_set<std::uint32_t> done;
        for (const std::vector<Entity>& chain : chains)
        {
            for (const Entity& entity : chain)
            {
                Ser::Element* block = hostBlock(entity);
                if (block == nullptr)
                {
                    continue;
                }
                if (done.insert(entity.GetId()).second && RepatchInstance(scene, entity))
                {
                    filler.ApplyDiff(block->value.AsObject(), block->value, entity,
                                     SpellEntityId(entity.GetComponent<EntityId>()));
                }
                break;
            }
        }
    }

    SceneLoadResult AssetStore::LoadScene(App& app, std::string_view path, std::string_view name)
    {
        return ReadSceneFile(
            path, [&]() -> Scene& { return app.CreateScene(name.empty() ? std::string(path) : std::string(name)); }, {},
            Detail::SceneRead::Load);
    }

    SceneLoadResult AssetStore::LoadSceneInto(Scene& scene, std::string_view path, Entity parent)
    {
        return ReadSceneFile(path, [&]() -> Scene& { return scene; }, parent, Detail::SceneRead::Merge);
    }

    Scene& AssetStore::CopyScene(App& app, Scene& scene, std::string_view name)
    {
        // The writer and the reader a save then a load would run, joined by the tree in memory: no file, no read of
        // the store's source for the scene, and nothing recorded for hot reload.
        const std::string named(scene.GetName());
        Ser::Value envelope;
        WriteSceneTree(scene, std::nullopt, named, envelope);
        Scene* made = nullptr;
        ReadSceneTree(envelope, named, {}, named,
                      [&]() -> Scene& {
                          made = &app.CreateScene(std::string(name));
                          return *made;
                      },
                      {}, Detail::SceneRead::Load, {});
        // The whole scene always writes, and the reader accepts what the writer writes; were either to refuse, the
        // copy is the new scene left empty, the refusal logged.
        return made != nullptr ? *made : app.CreateScene(std::string(name));
    }

    InstanceResult AssetStore::Instantiate(Scene& scene, std::string_view path, Entity parent)
    {
        InstanceResult result = ExpandInstance(scene, path);
        if (!result.root.IsValid())
        {
            return result;
        }
        if (parent.HasEntityList() && !Detail::LiveIn(scene.GetEntityList(), parent))
        {
            Detail::Record({.severity = Ser::DiagnosticSeverity::Error,
                            .path = std::string(path),
                            .message = "the parent given is no live entity of the scene instanced into; the instance's "
                                       "root goes at the top"},
                           *m_state->logger, result.load.diagnostics);
            result.load.status = LoadStatus::Partial;
        }
        else if (parent.IsValid())
        {
            parent.AddChild(result.root);
        }
        return result;
    }

    InstanceResult AssetStore::ExpandInstance(Scene& scene, std::string_view path)
    {
        InstanceResult result;
        std::string assetPath;
        std::string fileKey;
        // The instance is refused with `message`, logged here once, and nothing added.
        const auto fail = [&](std::string message) -> InstanceResult {
            m_state->logger->Error("Could not instance '{}': {}", path, message);
            result.load.status = LoadStatus::Failed;
            result.load.diagnostics.push_back(
                {.severity = Ser::DiagnosticSeverity::Error, .path = fileKey, .message = std::move(message)});
            return std::move(result);
        };
        if (const std::optional<std::string> problem =
                Detail::WholeFileLoadProblem(m_state->root, path, assetPath, fileKey))
        {
            return fail(*problem);
        }
        const std::optional<Instance> mark = Detail::InstanceOf(assetPath);
        if (!mark)
        {
            return fail(
                std::format("its asset path is {} bytes, more than the 255 an Instance holds", assetPath.size()));
        }

        Entity root;
        // What a recorded scene's instance read, for its re-patch: looked at before the read, as a scene file is.
        Detail::InstanceScope scope{.source = assetPath, .sourceKey = fileKey};
        constexpr std::string_view SceneExtension = ".scene.json5";
        if (assetPath.ends_with(SceneExtension))
        {
            if (m_state->scopeSink != nullptr && m_state->watcher)
            {
                if (const std::optional<ReloadWatcher::Stamp> stamp = Detail::StampOf(Detail::Utf8Path(fileKey)))
                {
                    scope.stamp.emplace(stamp->modified, stamp->size);
                }
            }
            // The read refused the file and logged why; the instance adds nothing more.
            result.load = ReadSceneFile(
                assetPath, [&]() -> Scene& { return scene; }, {}, Detail::SceneRead::Instance, &scope.ids);
            if (result.load.status == LoadStatus::Failed)
            {
                return result;
            }
            if (result.load.roots.size() == 1)
            {
                // The file's root is the instance's: its id is the host file's to give, and its source id still finds
                // it here.
                root = result.load.roots.front();
                if (root.HasComponent<EntityId>())
                {
                    result.entities.emplace(root.GetComponent<EntityId>().value, root);
                    scope.rootId = root.GetComponent<EntityId>().value;
                    root.RemoveComponent<EntityId>();
                }
            }
            else
            {
                // Several roots, or none, go under one named after the file, at the identity (SpawnNodes' rule).
                const std::string_view file = std::string_view(assetPath).substr(assetPath.rfind('/') + 1);
                root = scene.GetEntityList().CreateEntity();
                root.GetComponent<Name>().Set(std::string(file.substr(0, file.size() - SceneExtension.size())).c_str());
                ++result.load.entities;
                for (Entity& top : result.load.roots)
                {
                    root.AddChild(top);
                }
            }
        }
        else
        {
            // A model, owned by the host scene, spawned through the registered spawner (the renderer's SpawnModel) or
            // the store's own SpawnNodes. The load logged its own failure.
            const LoadResult<Model> model = Load<Model>(assetPath, AssetOwner(scene));
            if (!model.Ok())
            {
                return fail(std::format("it loads as no scene file or model: {}", model.diagnostic->importerMessage));
            }
            const SpawnedNodes spawned = m_state->modelSpawner != nullptr
                                             ? m_state->modelSpawner->Spawn(scene, model.handle, {})
                                             : SpawnNodes(scene, model.handle, {});
            if (!spawned.root.IsValid())
            {
                return fail("the model spawned nothing");
            }
            root = spawned.root;
            scope.model = true;
            scope.revision =
                m_state->Find(Detail::AssetKind::Model, {model.handle.index, model.handle.generation})->revision;
            result.load.scene = &scene;
            result.load.roots.push_back(root);
            result.load.entities = 1;
            Detail::ForEachInInstance(root, [&](const Entity&) { ++result.load.entities; });
            result.load.status = model.Status() == LoadStatus::Succeeded ? LoadStatus::Succeeded : LoadStatus::Partial;
        }

        if (root.HasComponent<Instance>())
        {
            root.GetComponent<Instance>() = *mark;
        }
        else
        {
            root.AddComponent<Instance>(*mark);
        }
        result.root = root;
        Detail::ForEachInInstance(root, [&](const Entity& entity) {
            if (entity.HasComponent<EntityId>())
            {
                result.entities.emplace(entity.GetComponent<EntityId>().value, entity);
                if (scope.model)
                {
                    scope.ids.insert(entity.GetComponent<EntityId>().value);
                }
            }
        });
        if (m_state->scopeSink != nullptr)
        {
            (*m_state->scopeSink)[root.GetId()] = std::move(scope);
        }
        return result;
    }

    SceneLoadResult AssetStore::ReadSceneFile(std::string_view path, const std::function<Scene&()>& makeScene,
                                              Entity parent, Detail::SceneRead read,
                                              std::unordered_set<std::uint64_t>* readIds)
    {
        SceneLoadResult result;
        std::string assetPath;
        std::string fileKey;
        const std::optional<std::string> pathProblem =
            Detail::WholeFileLoadProblem(m_state->root, path, assetPath, fileKey);
        // The load fails with `diagnostic`, logged here once, and no scene made.
        const auto fail = [&](Ser::Diagnostic diagnostic) -> SceneLoadResult {
            FailSceneRead(path, std::move(diagnostic), result);
            return std::move(result);
        };
        const auto failWith = [&](std::string message) {
            return fail({.severity = Ser::DiagnosticSeverity::Error, .path = fileKey, .message = std::move(message)});
        };

        if (pathProblem)
        {
            return failWith(*pathProblem);
        }
        // A load fills a scene the store records for the scene patch, and watches; looked at before the read, so a
        // write during it is a change the watch sees.
        const bool recorded = read == Detail::SceneRead::Load;
        const std::optional<ReloadWatcher::Stamp> stamp =
            recorded && m_state->watcher ? Detail::StampOf(Detail::Utf8Path(fileKey)) : std::nullopt;
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
        std::unordered_set<std::uint64_t> fileIds;
        // A recorded load records each instance it expands, nested ones too, for the instance re-patch.
        Detail::InstanceScopes scopes;
        Detail::InstanceScopes* const outerSink = m_state->scopeSink;
        if (recorded)
        {
            m_state->scopeSink = &scopes;
        }
        SceneLoadResult loaded = ReadSceneTree(*tree.tree, path, assetPath, fileKey, makeScene, parent, read,
                                               std::move(result), recorded || readIds != nullptr ? &fileIds : nullptr);
        m_state->scopeSink = outerSink;
        if (readIds != nullptr)
        {
            *readIds = fileIds;
        }
        if (recorded && loaded.scene != nullptr)
        {
            Detail::RecordSceneFile(m_state, *loaded.scene, fileKey, assetPath, std::move(fileIds), std::move(scopes),
                                    stamp);
        }
        return loaded;
    }

    void AssetStore::FailSceneRead(std::string_view named, Ser::Diagnostic diagnostic, SceneLoadResult& result)
    {
        m_state->logger->Error("Could not load scene '{}': {}", named, Ser::FormatDiagnostic(diagnostic));
        result.status = LoadStatus::Failed;
        result.diagnostics.push_back(std::move(diagnostic));
    }

    SceneLoadResult AssetStore::ReadSceneTree(Ser::Value& tree, std::string_view named, const std::string& assetPath,
                                              const std::string& fileKey, const std::function<Scene&()>& makeScene,
                                              Entity parent, Detail::SceneRead read, SceneLoadResult result,
                                              std::unordered_set<std::uint64_t>* fileIds,
                                              std::optional<std::uint64_t>* rootId)
    {
        Ser::Value* data = nullptr;
        if (std::optional<Detail::SceneEnvelopeProblem> problem = Detail::CheckSceneEnvelope(tree, data))
        {
            FailSceneRead(named,
                          Detail::LocatedAt(Ser::DiagnosticSeverity::Error, fileKey, problem->node, tree,
                                            std::move(problem->message)),
                          result);
            return result;
        }

        // A re-patch's source fills the instance's root from its single root block; one with several roots, or none,
        // hangs them from the root. A variant's root block is itself an instance, which the root cannot be filled from.
        std::optional<std::uint64_t> singleRoot;
        if (read == Detail::SceneRead::Repatch)
        {
            const Ser::Object* rootBlock = SingleRootBlock(*data);
            if (rootBlock != nullptr && rootBlock->Find("instance") != nullptr)
            {
                // Said once per run, however many instances the variant has.
                if (m_state->sourceTrees == nullptr || m_state->sourceTrees->try_emplace("variant:" + fileKey).second)
                {
                    m_state->logger->Warn(
                        "Could not re-patch the instances of '{}': its root is an instance of another file "
                        "(a variant), which only a load expands again; they are left as they were",
                        named);
                }
                result.status = LoadStatus::Failed;
                return result;
            }
            if (rootBlock != nullptr)
            {
                const Ser::Value* id = rootBlock->Find("id");
                if (id != nullptr && id->Kind() == Ser::ValueKind::String)
                {
                    if (const std::optional<EntityId> parsed = ParseEntityId(id->AsString()))
                    {
                        singleRoot = parsed->value;
                    }
                }
            }
            // A root that changed from the file's single root to a synthetic one, or back, holds the other's
            // components, which a patch by id cannot undo.
            const bool hadIds = fileIds != nullptr && !fileIds->empty();
            if (hadIds && rootId != nullptr && rootId->has_value() != singleRoot.has_value())
            {
                if (m_state->sourceTrees == nullptr || m_state->sourceTrees->try_emplace("roots:" + fileKey).second)
                {
                    m_state->logger->Warn("Could not re-patch the instances of '{}': it changed between one root and "
                                          "several, which only a load expands again; they are left as they were",
                                          named);
                }
                result.status = LoadStatus::Failed;
                return result;
            }
        }

        Scene& scene = makeScene();
        result.scene = &scene;
        const std::uint32_t owner = OwnerOf(AssetOwner(scene));
        const std::function<Detail::ReferenceRead(std::string_view, const FieldInfo&)> readReference =
            [&](std::string_view reference, const FieldInfo& field) {
                return ReadFileReference(assetPath, owner, reference, field);
            };
        SceneFiller filler{scene,
                           *m_state->types,
                           fileKey,
                           readReference,
                           *m_state->logger,
                           result,
                           {},
                           read,
                           m_state->root,
                           m_state->scenesReading,
                           [&](std::string_view source) { return ExpandInstance(scene, source); }};
        EntityList& entities = scene.GetEntityList();
        // An instance keeps its file's ids, which are scoped by its root; so are those inside the scene's instances
        // already, which no block's id can collide with.
        // A patch matches its blocks with the scene's entities by those ids instead.
        if (read == Detail::SceneRead::Repatch)
        {
            // The source's entities are those below the root holding an id its last read gave out, nested instances'
            // included, since a block of the source may hang from inside one; the first of each id holds it. What else
            // is below the root (a run-time child, or one a host added) is no block's, and stays.
            std::vector<Entity> below{parent};
            for (std::size_t i = 0; i < below.size(); ++i)
            {
                below[i].ForEachChild([&](const Entity& child) {
                    below.push_back(child);
                    if (fileIds != nullptr && child.HasComponent<EntityId>() &&
                        fileIds->contains(child.GetComponent<EntityId>().value))
                    {
                        filler.live.emplace(child.GetComponent<EntityId>().value, child.GetId());
                    }
                });
            }
            if (singleRoot)
            {
                filler.live.insert_or_assign(*singleRoot, parent.GetId());
            }
            filler.scopeRoot = parent;
        }
        else if (read != Detail::SceneRead::Instance)
        {
            entities.ForEachEntity([&](auto handle) {
                const Entity entity = entities.GetEntity(static_cast<std::uint32_t>(handle));
                if (!entity.HasComponent<EntityId>())
                {
                    return;
                }
                const std::uint64_t value = entity.GetComponent<EntityId>().value;
                if (read == Detail::SceneRead::Patch)
                {
                    // An entity inside an instance holds the file's id when the file added it there (a block whose
                    // `parent` is a slash path): it is the file's to match and destroy, as one at the top is. Any
                    // other there is its source's, scoped by its instance.
                    if (!Detail::InsideInstance(entity) || (fileIds != nullptr && fileIds->contains(value) &&
                                                            !SourceEntity(m_state->scopeSink, entity, value)))
                    {
                        filler.live.emplace(value, entity.GetId());
                    }
                }
                else if (!Detail::InsideInstance(entity))
                {
                    filler.taken.insert(value);
                }
            });
        }
        if (filler.Patching())
        {
            filler.priorIds = fileIds;
            filler.repatch = [this, &scene](Entity root) { return RepatchInstance(scene, root); };
        }
        if (read == Detail::SceneRead::Patch)
        {
            // An id the file never held (a merge's) is the scene's own: a block of it that matches no entity, being of
            // another kind, is re-minted, so no id repeats in the scene. One the file held goes with its entity.
            for (const auto& [value, entity] : filler.live)
            {
                if (fileIds == nullptr || !fileIds->contains(value))
                {
                    filler.taken.insert(value);
                }
            }
        }
        if (parent.HasEntityList() && !Detail::LiveIn(entities, parent))
        {
            // A parent of another scene, or one destroyed: the file's roots load at the top of this one.
            filler.Report(Ser::DiagnosticSeverity::Error, nullptr, *data,
                          "the parent given is no live entity of the scene loaded into; the file's roots load at the "
                          "top");
        }
        else if (!singleRoot)
        {
            // A re-patch's single root block is the root itself, so it has no parent to go under.
            filler.parent = parent;
        }
        // On the chain while its blocks are read, so an instance of it, directly or through another file, is refused.
        struct OnChain
        {
            std::vector<std::string>& chain;
            ~OnChain()
            {
                chain.pop_back();
            }
        };
        // A copy made in memory is no file, so there is nothing to put on the chain.
        std::optional<OnChain> onChain;
        if (!assetPath.empty())
        {
            m_state->scenesReading.push_back(assetPath);
            onChain.emplace(m_state->scenesReading);
        }
        filler.FillScene(*data);
        if (read == Detail::SceneRead::Load || read == Detail::SceneRead::Patch ||
            (read == Detail::SceneRead::Merge && !filler.parent.IsValid()))
        {
            // A save writes the scene's top-level entities in the order its files list them.
            OrderRoots(scene, result.roots, read == Detail::SceneRead::Patch);
        }
        if (fileIds != nullptr)
        {
            fileIds->clear();
            for (const auto& [value, entity] : filler.ids)
            {
                fileIds->insert(value);
            }
        }
        if (rootId != nullptr)
        {
            *rootId = singleRoot;
        }
        result.status = result.diagnostics.empty() ? LoadStatus::Succeeded : LoadStatus::Partial;
        return result;
    }
} // namespace Fenrir
