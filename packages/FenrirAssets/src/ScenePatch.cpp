// The scene patch (ADR-0081 §3) and the instance re-patch (ADR-0081 §4). LoadScene records on its scene which file
// filled it and every instance the read expanded, and the store watches that file and each scene source. A change to
// the file patches every scene filled from it by entity id, through the reader's patch read; a change to a source,
// a scene file or a model, re-patches every instance of it in those scenes, each host block's diff reapplied on top.
// The record is a resource of its scene, so it dies with the scene and a destroyed scene is never patched.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"
#include "AssetWatcher.hpp"
#include "Envelope.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSerialisation/Value.hpp"

namespace Fenrir
{
    namespace
    {
        // A scene source's stamp as the watch takes it, from the one an instance's scope keeps.
        std::optional<ReloadWatcher::Stamp> WatchStamp(const Detail::InstanceScope& scope)
        {
            if (!scope.stamp)
            {
                return std::nullopt;
            }
            return ReloadWatcher::Stamp{scope.stamp->first, scope.stamp->second};
        }

        // Sets where the store records the instances expansions make, and puts the one before back after.
        struct ScopeSink
        {
            Detail::InstanceScopes*& sink;
            Detail::InstanceScopes* const before;

            ScopeSink(Detail::InstanceScopes*& current, Detail::InstanceScopes& scopes) : sink(current), before(current)
            {
                sink = &scopes;
            }
            ScopeSink(const ScopeSink&) = delete;
            ScopeSink& operator=(const ScopeSink&) = delete;
            ~ScopeSink()
            {
                sink = before;
            }
        };

        // Sets the store's source trees for one run, so each source is read once, and clears them after.
        struct TreesRun
        {
            Detail::SourceTrees*& current;
            Detail::SourceTrees trees;

            explicit TreesRun(Detail::SourceTrees*& state) : current(state)
            {
                current = &trees;
            }
            TreesRun(const TreesRun&) = delete;
            TreesRun& operator=(const TreesRun&) = delete;
            ~TreesRun()
            {
                current = nullptr;
            }
        };
    } // namespace

    namespace Detail
    {
        void RecordSceneFile(const std::shared_ptr<AssetStoreState>& state, Scene& scene, const std::string& fileKey,
                             const std::string& assetPath, std::unordered_set<std::uint64_t> fileIds,
                             InstanceScopes scopes, std::optional<ReloadWatcher::Stamp> stamp)
        {
            SceneFileRecord& record = scene.AddResource<SceneFileRecord>(state, scene, fileKey, assetPath);
            record.fileIds = std::move(fileIds);
            record.scopes = std::move(scopes);
            state->sceneFiles.push_back(&record);
            if (state->watcher)
            {
                state->watcher->TrackScene(fileKey, assetPath, stamp, ReloadWatcher::Clock::now());
            }
            SyncSceneWatches(*state, record, {});
        }

        std::vector<std::string> SceneSourceKeys(const SceneFileRecord& record)
        {
            std::vector<std::string> keys;
            for (const auto& [root, scope] : record.scopes)
            {
                if (!scope.model && std::ranges::find(keys, scope.sourceKey) == keys.end())
                {
                    keys.push_back(scope.sourceKey);
                }
            }
            return keys;
        }

        void SyncSceneWatches(AssetStoreState& state, SceneFileRecord& record, const std::vector<std::string>& before)
        {
            EntityList& entities = record.scene->GetEntityList();
            std::erase_if(record.scopes, [&](const auto& entry) {
                const Entity root = entities.GetEntity(entry.first);
                return !LiveIn(entities, root) || !root.HasComponent<Instance>();
            });
            if (!state.watcher)
            {
                return;
            }
            for (const auto& [root, scope] : record.scopes)
            {
                // A source watched already is left as it is, so a change it has seen and not yet settled stays seen.
                if (!scope.model && !state.watcher->WatchesScene(scope.sourceKey))
                {
                    state.watcher->TrackScene(scope.sourceKey, scope.source, WatchStamp(scope),
                                              ReloadWatcher::Clock::now());
                }
            }
            for (const std::string& key : before)
            {
                if (!state.RecordNames(key))
                {
                    state.watcher->ForgetScene(key);
                }
            }
        }

        bool AssetStoreState::RecordNames(const std::string& key) const
        {
            return std::ranges::any_of(sceneFiles, [&](const SceneFileRecord* record) {
                return record->fileKey == key || std::ranges::any_of(record->scopes, [&](const auto& entry) {
                           return !entry.second.model && entry.second.sourceKey == key;
                       });
            });
        }

        void AssetStoreState::ForgetSceneFile(const SceneFileRecord& record)
        {
            std::erase(sceneFiles, &record);
            if (!watcher)
            {
                return;
            }
            std::vector<std::string> keys = SceneSourceKeys(record);
            keys.push_back(record.fileKey);
            for (const std::string& key : keys)
            {
                if (!RecordNames(key))
                {
                    watcher->ForgetScene(key);
                }
            }
        }
    } // namespace Detail

    void AssetStore::PatchSceneFile(const std::string& fileKey)
    {
        namespace Ser = Serialisation;

        // The records are copied out first: a patch adds and destroys entities, never scenes, but the list is the
        // store's, and the records are the scenes'.
        std::vector<Detail::SceneFileRecord*> records;
        for (Detail::SceneFileRecord* record : m_state->sceneFiles)
        {
            if (record->fileKey == fileKey)
            {
                records.push_back(record);
            }
        }
        if (!records.empty())
        {
            const TreesRun run(m_state->sourceTrees);
            const std::string assetPath = records.front()->assetPath;
            // Read and checked once, so a file that is no scene changes nothing and is reported once for this change.
            if (const Ser::Value* tree = SceneSourceTree(assetPath, fileKey, "patch the scenes filled from"))
            {
                for (Detail::SceneFileRecord* record : records)
                {
                    // Each scene's read takes what it reads out of the tree, so each reads a copy. The instances it
                    // adds, and those it re-patches, are recorded on the scene's record.
                    Ser::Value copy = *tree;
                    Scene& scene = *record->scene;
                    const std::vector<std::string> before = Detail::SceneSourceKeys(*record);
                    {
                        const ScopeSink sink(m_state->scopeSink, record->scopes);
                        (void)ReadSceneTree(
                            copy, assetPath, assetPath, fileKey, [&]() -> Scene& { return scene; }, {},
                            Detail::SceneRead::Patch, {}, &record->fileIds);
                    }
                    Detail::SyncSceneWatches(*m_state, *record, before);
                }
            }
        }
        // The file may be a source of instances too, in this scene or others.
        RepatchInstancesOf(fileKey);
    }

    void AssetStore::RepatchInstancesOf(const std::string& sourceKey)
    {
        namespace Ser = Serialisation;

        std::vector<Detail::SceneFileRecord*> records;
        for (Detail::SceneFileRecord* record : m_state->sceneFiles)
        {
            if (std::ranges::any_of(record->scopes,
                                    [&](const auto& entry) { return entry.second.sourceKey == sourceKey; }))
            {
                records.push_back(record);
            }
        }
        if (records.empty())
        {
            return;
        }
        const TreesRun run(m_state->sourceTrees);
        for (Detail::SceneFileRecord* record : records)
        {
            Scene& scene = *record->scene;
            EntityList& entities = scene.GetEntityList();
            // Each instance of the source, nested or not, is re-patched through the top-level instance holding it, so
            // every diff over it, the host's and those of the files between, is reapplied in order. They are taken in
            // the scene's order, so a run re-patches the same way each time.
            std::unordered_set<std::uint32_t> holding;
            for (const auto& [id, scope] : record->scopes)
            {
                Entity entity = entities.GetEntity(id);
                if (scope.sourceKey != sourceKey || !Detail::LiveIn(entities, entity))
                {
                    continue;
                }
                Entity top = entity;
                for (Entity above = entity.GetComponent<Relationship>().parent; above.IsValid();
                     above = above.GetComponent<Relationship>().parent)
                {
                    if (above.HasComponent<Instance>())
                    {
                        top = above;
                    }
                }
                holding.insert(top.GetId());
            }
            std::vector<Entity> tops;
            entities.ForEachEntity([&](auto handle) {
                if (holding.contains(static_cast<std::uint32_t>(handle)))
                {
                    tops.push_back(entities.GetEntity(static_cast<std::uint32_t>(handle)));
                }
            });
            if (tops.empty())
            {
                continue;
            }
            // The host file is read, never written: its blocks hold the diffs reapplied on top.
            const Ser::Value* host =
                SceneSourceTree(record->assetPath, record->fileKey, "re-patch the instances in the scenes filled from");
            if (host == nullptr)
            {
                continue;
            }
            Ser::Value copy = *host;
            const std::vector<std::string> before = Detail::SceneSourceKeys(*record);
            {
                const ScopeSink sink(m_state->scopeSink, record->scopes);
                RepatchHostInstances(scene, copy, record->assetPath, record->fileKey, tops);
            }
            Detail::SyncSceneWatches(*m_state, *record, before);
        }
    }
} // namespace Fenrir
