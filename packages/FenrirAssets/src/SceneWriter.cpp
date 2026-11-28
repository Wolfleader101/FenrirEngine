// The scene writer (ADR-0078, ADR-0079): AssetStore::SaveScene, a scene to its scene envelope.

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

        // Appends the entities of the subtree `entity` roots that the file holds to `written`, depth-first with each
        // entity's children after it in their sibling order; a Transient entity is left out with its children, and
        // counted in `skipped`.
        void CollectWritten(const Entity& entity, std::vector<Entity>& written, std::size_t& skipped)
        {
            if (entity.HasComponent<Transient>())
            {
                skipped += SubtreeSize(entity);
                return;
            }
            written.push_back(entity);
            entity.ForEachChild([&](const Entity& child) { CollectWritten(child, written, skipped); });
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
    } // namespace

    SceneSaveResult AssetStore::SaveScene(Scene& scene, std::string_view path)
    {
        SceneSaveResult result;
        const std::string named(path);
        const auto fail = [&](std::string message) -> SceneSaveResult {
            m_state->logger->Error("Could not save scene '{}' to '{}': {}", scene.GetName(), named, message);
            result.status = SaveStatus::Failed;
            result.diagnostics.push_back(
                {.severity = Ser::DiagnosticSeverity::Error, .path = named, .message = std::move(message)});
            return std::move(result);
        };

        std::filesystem::path target;
        if (std::optional<std::string> problem = Detail::SavePathProblem(m_state->root, path, target))
        {
            return fail(std::move(*problem));
        }

        const TypeRegistry& registry = *m_state->types;
        EntityList& entities = scene.GetEntityList();

        // The roots, in the order of their entity ids, so a scene saved twice lists them alike.
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
        std::vector<Entity> written;
        for (const std::uint32_t root : roots)
        {
            CollectWritten(entities.GetEntity(root), written, result.entitiesSkipped);
        }

        // Every entity the file holds has an id unique within it before any block is written, so an `entity` field
        // names one written after its own block too. An id is kept by the first entity in file order that has it; a
        // later one, and an entity with none, is minted one no entity of the file has (ADR-0078 §2).
        std::unordered_set<std::uint64_t> held;
        for (const Entity& entity : written)
        {
            if (entity.HasComponent<EntityId>())
            {
                held.insert(entity.GetComponent<EntityId>().value);
            }
        }
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
                            "below one, one destroyed, or one of another scene); it is written null",
                            where, field.name),
                named, *m_state->logger, result.diagnostics, result.nullReferences);
            return {};
        };

        // Resources come before entities in the file but are applied after them on load, so an `entity` field in one
        // resolves (ADR-0078 §7); name order keeps the bytes stable.
        Ser::Object resources;
        std::vector<const TypeDescription*> resourceTypes = registry.Types(Role::SceneResource);
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
        Ser::Array blocks;
        for (const Entity& entity : written)
        {
            const std::uint32_t id = entity.GetId();
            Ser::Object block;
            block.Add("id", ids.at(id));
            if (const Entity parent = entity.GetComponent<Relationship>().parent; parent.IsValid())
            {
                block.Add("parent", ids.at(parent.GetId()));
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
            std::ranges::stable_sort(entries, [](const Ser::Member& a, const Ser::Member& b) {
                const int rankA = Rank(a.key);
                const int rankB = Rank(b.key);
                return rankA != rankB ? rankA < rankB : a.key < b.key;
            });
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
        Ser::Object envelope;
        envelope.Add("format", Detail::EnvelopeFormat);
        envelope.Add("type", Detail::SceneTypeName);
        envelope.Add("version", Detail::SceneVersion);
        envelope.Add("data", std::move(data));
        if (const std::optional<std::string> problem =
                Detail::WriteAtomically(target, Ser::WriteText(Ser::Value(std::move(envelope)))))
        {
            return fail(*problem);
        }
        result.status =
            result.nullReferences > 0 || result.keptComponentsLost > 0 ? SaveStatus::Partial : SaveStatus::Succeeded;
        return result;
    }
} // namespace Fenrir
