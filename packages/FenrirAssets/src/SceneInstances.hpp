#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    class EntityList;
    class Scene;
} // namespace Fenrir

namespace Fenrir::Detail
{
    /**
     * @brief The key of `entity` in the instance whose scope it is in (ADR-0080 §5): `prefix`, the key of the nested
     * instance's root holding it (empty in the instance's own scope), then `/` and its id in its source. Its slash path
     * from the instance's root: `"m9x1p"`, or `"a1pha/m9x1p"` inside the nested instance `a1pha`. Empty for an entity
     * with no id, which no key names.
     */
    [[nodiscard]] std::string ScopedKey(std::string_view prefix, const Entity& entity);

    /**
     * @brief A fresh expansion of an instance's source, which the scene writer compares the live instance with
     * (ADR-0080 §4): its root, and every entity below it that a key names, depth-first in sibling order, nested
     * instances' entities included; an entity with no id, and what is below it, is not walked. Of two entities with
     * one key, the first keeps it, as FollowSlashPath finds it.
     */
    struct InstanceBaseline
    {
        /** @brief The expansion's root, holding its source's root components. */
        Entity root;
        /** @brief Each keyed entity below the root, with its key, depth-first. */
        std::vector<std::pair<std::string, Entity>> keyed;
        /** @brief The same entities by key. */
        std::unordered_map<std::string, Entity> byKey;
        /** @brief Each keyed entity's key, by its entity id. */
        std::unordered_map<std::uint32_t, std::string> keyOf;
    };

    /**
     * @brief The baselines one save compares its instances with: each source expanded once, the first time it is asked
     * for, into a scene of their own that no app holds and that dies with them, so what the expansions loaded is
     * released.
     */
    class InstanceBaselines
    {
      public:
        /** @brief How a source is expanded: the store's Instantiate, at the top of the scene given. */
        using Expand = std::function<InstanceResult(Scene&, std::string_view)>;

        /** @brief Baselines expanded through `expand`, into a scene of their own made now. */
        explicit InstanceBaselines(Expand expand);
        /** @brief Destroys the baselines' scene, releasing what the expansions loaded. */
        ~InstanceBaselines();
        InstanceBaselines(const InstanceBaselines&) = delete;
        InstanceBaselines& operator=(const InstanceBaselines&) = delete;

        /** @brief The baseline of `source`, an instance's asset path; null when it does not expand (its read logged
         * why). */
        [[nodiscard]] const InstanceBaseline* For(std::string_view source);

        /** @brief The scene the baselines are expanded into. */
        [[nodiscard]] Scene& GetScene() noexcept;

      private:
        Expand m_expand;
        std::unique_ptr<Scene> m_scene;
        // By source; null for one that did not expand, so it is tried once.
        std::unordered_map<std::string, std::unique_ptr<InstanceBaseline>> m_baselines;
    };

    /** @brief How a live instance matches its baseline: the scene writer's diff is taken over it. */
    struct InstanceMatch
    {
        /** @brief The baseline matched against; null when the source did not expand, so nothing matched. */
        const InstanceBaseline* baseline = nullptr;
        /** @brief The live entities the baseline has, by key: the source's entities still in the instance. */
        std::unordered_map<std::string, Entity> liveByKey;
        /** @brief Each matched entity's key, by its entity id. */
        std::unordered_map<std::uint32_t, std::string> keyOf;
        /** @brief The matched entities tagged `Transient`, or below one: never compared, so never written. */
        std::unordered_set<std::string> quiet;
        /**
         * @brief The keys of the baseline's entities the live instance lacks, in the baseline's order: each one whose
         * parent the instance still has, since one below a removed entity goes with it.
         */
        std::vector<std::string> removed;
    };

    /**
     * @brief Matches the live instance `root` tops with `baseline`, by key. Each entity below the root that the
     * baseline has no entity of its key for (an entity a host added, or one with no id) is handed to `added`, and what
     * is below it is not walked: it is the host's, not the source's. One tagged `Transient` is not handed over when the
     * baseline has it, since it is still the source's. With no baseline, every child of the root is added.
     */
    [[nodiscard]] InstanceMatch MatchInstance(const Entity& root, const InstanceBaseline* baseline,
                                              const std::function<void(const Entity&)>& added);

    /** @brief Destroys `entity` and every entity below it, after taking it from its parent's children. */
    void DestroySubtree(EntityList& entities, Entity entity);
} // namespace Fenrir::Detail
