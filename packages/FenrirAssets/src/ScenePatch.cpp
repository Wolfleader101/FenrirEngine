// The scene patch (ADR-0081 §3): LoadScene records on its scene which file filled it, the store watches that file,
// and a change to it patches every scene filled from it by entity id, through the reader's patch read. The record is
// a resource of its scene, so it dies with the scene and a destroyed scene is never patched.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AssetSource.hpp"
#include "AssetStoreState.hpp"
#include "AssetWatcher.hpp"
#include "Envelope.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/ReadTree.hpp"
#include "FenrirSerialisation/Value.hpp"

namespace Fenrir
{
    namespace Detail
    {
        void RecordSceneFile(const std::shared_ptr<AssetStoreState>& state, Scene& scene, const std::string& fileKey,
                             const std::string& assetPath, std::unordered_set<std::uint64_t> fileIds,
                             std::optional<ReloadWatcher::Stamp> stamp)
        {
            SceneFileRecord& record = scene.AddResource<SceneFileRecord>(state, scene, fileKey, assetPath);
            record.fileIds = std::move(fileIds);
            state->sceneFiles.push_back(&record);
            if (state->watcher)
            {
                state->watcher->TrackScene(fileKey, assetPath, stamp, ReloadWatcher::Clock::now());
            }
        }

        void AssetStoreState::ForgetSceneFile(const SceneFileRecord& record)
        {
            std::erase(sceneFiles, &record);
            const bool stillFilled = std::ranges::any_of(
                sceneFiles, [&](const SceneFileRecord* other) { return other->fileKey == record.fileKey; });
            if (watcher && !stillFilled)
            {
                watcher->ForgetScene(record.fileKey);
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
        if (records.empty())
        {
            return;
        }
        const std::string& assetPath = records.front()->assetPath;
        ILogger& logger = *m_state->logger;

        // Read and checked once, so a file that is no scene changes nothing and is reported once for this change.
        const std::optional<std::vector<std::byte>> bytes = m_state->source->Read(assetPath);
        if (!bytes)
        {
            logger.Error("Could not patch the scenes filled from '{}': the file could not be read; they are left as "
                         "they were",
                         assetPath);
            return;
        }
        Ser::ReadTreeResult tree = Ser::ReadTree(*bytes, fileKey, logger);
        if (!tree.tree)
        {
            // ReadTree logged its own located diagnostics.
            logger.Error("Could not patch the scenes filled from '{}': the file is neither JSON5 nor the tree "
                         "encoding; they are left as they were",
                         assetPath);
            return;
        }
        Ser::Value* data = nullptr;
        if (const std::optional<Detail::SceneEnvelopeProblem> problem = Detail::CheckSceneEnvelope(*tree.tree, data))
        {
            logger.Error("Could not patch the scenes filled from '{}': {}; they are left as they were", assetPath,
                         Ser::FormatDiagnostic(Detail::LocatedAt(Ser::DiagnosticSeverity::Error, fileKey, problem->node,
                                                                 *tree.tree, problem->message)));
            return;
        }

        for (Detail::SceneFileRecord* record : records)
        {
            // Each scene's read takes what it reads out of the tree, so each reads a copy.
            Ser::Value copy = *tree.tree;
            Scene& scene = *record->scene;
            (void)ReadSceneTree(
                copy, assetPath, assetPath, fileKey, [&]() -> Scene& { return scene; }, {}, Detail::SceneRead::Patch,
                {}, &record->fileIds);
        }
    }
} // namespace Fenrir
