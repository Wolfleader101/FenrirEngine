// Scene instances' diffs (ADR-0080 §4, §8): the baselines the scene writer compares a live instance with, the match
// between the two by key, and AssetStore::Unpack, which unlinks an instance so its subtree saves verbatim.

#include "SceneInstances.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"
#include "Envelope.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // Records each keyed entity below `scope` in `baseline`, depth-first; `prefix` is the key of the nested
        // instance's root whose scope `scope` is in.
        void CollectKeyed(InstanceBaseline& baseline, const Entity& scope, const std::string& prefix)
        {
            scope.ForEachChild([&](const Entity& child) {
                const std::string key = ScopedKey(prefix, child);
                if (key.empty())
                {
                    // No key names it, nor what is below it, as MatchInstance hands a live one over whole.
                    return;
                }
                if (baseline.byKey.emplace(key, child).second)
                {
                    baseline.keyed.emplace_back(key, child);
                    baseline.keyOf.emplace(child.GetId(), key);
                }
                CollectKeyed(baseline, child, child.HasComponent<Instance>() ? key : prefix);
            });
        }

        // Matches the children of `scope` with `match`'s baseline; `quiet` below a Transient entity.
        void MatchBelow(InstanceMatch& match, const Entity& scope, const std::string& prefix, bool quiet,
                        const std::function<void(const Entity&)>& added)
        {
            scope.ForEachChild([&](const Entity& child) {
                const std::string key = ScopedKey(prefix, child);
                const bool matched =
                    !key.empty() && match.baseline->byKey.contains(key) && !match.liveByKey.contains(key);
                if (!matched)
                {
                    // Below a Transient entity nothing is written, the host's or the source's.
                    if (!quiet)
                    {
                        added(child);
                    }
                    return;
                }
                match.liveByKey.emplace(key, child);
                match.keyOf.emplace(child.GetId(), key);
                const bool silent = quiet || child.HasComponent<Transient>();
                if (silent)
                {
                    match.quiet.insert(key);
                }
                MatchBelow(match, child, child.HasComponent<Instance>() ? key : prefix, silent, added);
            });
        }
    } // namespace

    std::string ScopedKey(std::string_view prefix, const Entity& entity)
    {
        if (!entity.HasComponent<EntityId>() || entity.GetComponent<EntityId>().value == 0)
        {
            return {};
        }
        const std::string id = SpellEntityId(entity.GetComponent<EntityId>());
        return prefix.empty() ? id : std::format("{}/{}", prefix, id);
    }

    InstanceBaselines::InstanceBaselines(Expand expand)
        : m_expand(std::move(expand)), m_scene(std::make_unique<Scene>("Instance baselines"))
    {
    }

    InstanceBaselines::~InstanceBaselines() = default;

    const InstanceBaseline* InstanceBaselines::For(std::string_view source)
    {
        const std::string key(source);
        if (const auto found = m_baselines.find(key); found != m_baselines.end())
        {
            return found->second.get();
        }
        std::unique_ptr<InstanceBaseline> baseline;
        const InstanceResult expanded = m_expand(*m_scene, source);
        if (expanded.root.IsValid())
        {
            baseline = std::make_unique<InstanceBaseline>();
            baseline->root = expanded.root;
            CollectKeyed(*baseline, expanded.root, {});
        }
        return m_baselines.emplace(key, std::move(baseline)).first->second.get();
    }

    Scene& InstanceBaselines::GetScene() noexcept
    {
        return *m_scene;
    }

    InstanceMatch MatchInstance(const Entity& root, const InstanceBaseline* baseline,
                                const std::function<void(const Entity&)>& added)
    {
        InstanceMatch match;
        match.baseline = baseline;
        if (baseline == nullptr)
        {
            root.ForEachChild([&](const Entity& child) { added(child); });
            return match;
        }
        MatchBelow(match, root, {}, false, added);
        for (const auto& [key, entity] : baseline->keyed)
        {
            if (match.liveByKey.contains(key))
            {
                continue;
            }
            const Entity parent = entity.GetComponent<Relationship>().parent;
            const auto parentKey = baseline->keyOf.find(parent.GetId());
            if (parent == baseline->root || parentKey == baseline->keyOf.end() ||
                match.liveByKey.contains(parentKey->second))
            {
                match.removed.push_back(key);
            }
        }
        return match;
    }

    void DestroySubtree(EntityList& entities, Entity entity)
    {
        if (const Entity parent = entity.GetComponent<Relationship>().parent; parent.IsValid())
        {
            parent.RemoveChild(entity);
        }
        std::vector<Entity> doomed{entity};
        for (std::size_t i = 0; i < doomed.size(); ++i)
        {
            doomed[i].ForEachChild([&](const Entity& child) { doomed.push_back(child); });
        }
        for (const Entity& gone : doomed)
        {
            entities.DestroyEntity(gone);
        }
    }
} // namespace Fenrir::Detail

namespace Fenrir
{
    bool AssetStore::Unpack(Scene& scene, Entity root)
    {
        EntityList& entities = scene.GetEntityList();
        if (!Detail::LiveIn(entities, root) || !root.HasComponent<Instance>())
        {
            m_state->logger->Error("Could not unpack an entity of scene '{}': it is {}", scene.GetName(),
                                   Detail::LiveIn(entities, root) ? "no instance's root"
                                                                  : "no live entity of the scene");
            return false;
        }

        // The ids of the scope's entities were their source's; each takes a fresh one no entity of the scene has, so
        // none repeats in the host's file. A nested instance's root is in the scope, and is re-minted; what is below it
        // is in its own, and keeps its ids.
        std::unordered_set<std::uint64_t> held;
        entities.ForEachEntity([&](auto handle) {
            const Entity entity = entities.GetEntity(static_cast<std::uint32_t>(handle));
            if (entity.HasComponent<EntityId>())
            {
                held.insert(entity.GetComponent<EntityId>().value);
            }
        });
        // The source's entities are those a fresh expansion of it has: an entity the host added holds an id of the
        // host's file already, unique there, and keeps it. With no expansion to tell them apart, every one is
        // re-minted.
        std::vector<Entity> unlinked;
        Detail::InstanceBaselines baselines(
            [this](Scene& into, std::string_view source) { return ExpandInstance(into, source); });
        if (const Detail::InstanceBaseline* baseline = baselines.For(std::string(root.GetComponent<Instance>().source)))
        {
            const Detail::InstanceMatch match = Detail::MatchInstance(root, baseline, [](const Entity&) {});
            Detail::ForEachInInstance(root, [&](const Entity& entity) {
                const auto key = match.keyOf.find(entity.GetId());
                if (key != match.keyOf.end() && key->second.find('/') == std::string::npos)
                {
                    unlinked.push_back(entity);
                }
            });
        }
        else
        {
            Detail::ForEachInInstance(root, [&](const Entity& entity) { unlinked.push_back(entity); });
        }
        root.RemoveComponent<Instance>();
        for (Entity& entity : unlinked)
        {
            EntityId id;
            do
            {
                id = MintEntityId();
            } while (!held.insert(id.value).second);
            if (entity.HasComponent<EntityId>())
            {
                entity.GetComponent<EntityId>() = id;
            }
            else
            {
                entity.AddComponent<EntityId>(id);
            }
        }
        return true;
    }
} // namespace Fenrir
