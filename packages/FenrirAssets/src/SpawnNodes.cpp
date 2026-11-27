// Spawning a model's node tree as entities (ADR-0028, ADR-0031): the model's half of spawning, which FenrirRenderer's
// SpawnModel builds on, giving each node's entity its model id and marking the root as an instance (ADR-0080); and the
// ModelSpawner seam Instantiate spawns a model source through.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "AssetStoreState.hpp"
#include "Envelope.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ModelSpawner.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir
{
    EntityId ModelEntityId(std::uint64_t number)
    {
        // The number's base32 digits, highest first, read back as an id, so FenrirECS keeps the packing.
        constexpr std::string_view alphabet = "0123456789abcdefghjkmnpqrstvwxyz";
        std::string spelled;
        do
        {
            spelled.insert(spelled.begin(), alphabet[number & 0x1f]);
            number >>= 5;
        } while (number != 0);
        return ParseEntityId(spelled).value_or(EntityId{});
    }

    bool AssetStore::RegisterModelSpawner(ModelSpawner& spawner)
    {
        if (m_state->modelSpawner != nullptr && m_state->modelSpawner != &spawner)
        {
            m_state->logger->Error("A model spawner is already registered on the asset store; this second one is "
                                   "ignored");
            return false;
        }
        m_state->modelSpawner = &spawner;
        return true;
    }

    void AssetStore::UnregisterModelSpawner(const ModelSpawner& spawner) noexcept
    {
        if (m_state->modelSpawner == &spawner)
        {
            m_state->modelSpawner = nullptr;
        }
    }

    SpawnedNodes AssetStore::SpawnNodes(Scene& scene, AssetHandle<Model> model, Entity parent)
    {
        const Model* data = TryGet(model);
        if (data == nullptr)
        {
            m_state->logger->Error("Could not spawn a model: its handle is empty or stale");
            return {};
        }

        // The root is named after the model's file (`scene` for `models/scene.gltf`), or `Model` for one added in
        // code, which has no file.
        const Detail::AssetRecord& record = *m_state->Find(Detail::AssetKind::Model, {model.index, model.generation});
        std::string name = "Model";
        if (!record.key.empty())
        {
            // The key is UTF-8, which a narrow std::filesystem::path does not assume on Windows.
            const std::u8string stem =
                std::filesystem::path(
                    std::u8string_view(reinterpret_cast<const char8_t*>(record.key.data()), record.key.size()))
                    .stem()
                    .u8string();
            name.assign(reinterpret_cast<const char*>(stem.data()), stem.size());
        }

        EntityList& entities = scene.GetEntityList();
        SpawnedNodes spawned;
        spawned.root = entities.CreateEntity();
        spawned.root.GetComponent<Name>().Set(name.c_str());
        // The root is the model's instance, so a save writes it as one block naming the file; a model with no asset
        // path (added in code, or a built-in) has nothing a file could name.
        if (const std::optional<std::string> path =
                PathOfSlot(Detail::AssetKind::Model, {model.index, model.generation});
            path && !path->starts_with(Detail::BuiltinPrefix))
        {
            if (const std::optional<Instance> instance = Detail::InstanceOf(*path))
            {
                spawned.root.AddComponent<Instance>(*instance);
            }
        }
        if (parent.IsValid())
        {
            parent.AddChild(spawned.root);
        }
        spawned.nodes.reserve(data->nodes.size());
        for (std::size_t index = 0; index < data->nodes.size(); ++index)
        {
            const ModelNode& node = data->nodes[index];
            // Bones are FenrirSkeleton's, posed in its arrays: they get no entity.
            if (node.kind == ModelNodeKind::Bone)
            {
                spawned.nodes.emplace_back();
                continue;
            }
            // An ordinary node below bones hangs from its nearest spawned ancestor, at its rest place through the
            // bones between; FenrirSkeleton's bone attachment then makes it follow the bone above it.
            Transform rest = node.rest;
            std::int32_t parent = node.parent;
            while (parent >= 0 && data->nodes[static_cast<std::size_t>(parent)].kind == ModelNodeKind::Bone)
            {
                rest = ComposeTransform(data->nodes[static_cast<std::size_t>(parent)].rest, rest);
                parent = data->nodes[static_cast<std::size_t>(parent)].parent;
            }
            Entity entity = entities.CreateEntity();
            entity.GetComponent<Name>().Set(node.name.c_str());
            entity.GetComponent<Transform>() = rest;
            // The same id on every spawn, so a host file can name the entity inside any instance of the model.
            entity.AddComponent<EntityId>(ModelEntityId(index + 1));
            // Nodes come parents first, so the parent's entity already exists.
            const Entity& above = parent >= 0 ? spawned.nodes[static_cast<std::size_t>(parent)] : spawned.root;
            above.AddChild(entity);
            spawned.nodes.push_back(entity);
        }
        return spawned;
    }
} // namespace Fenrir
