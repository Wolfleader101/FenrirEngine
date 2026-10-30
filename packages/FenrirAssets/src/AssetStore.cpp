#include "FenrirAssets/AssetStore.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "AssetStoreState.hpp"
#include "CubemapLoading.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "Importers/ImageImporter.hpp"
#include "Importers/ModelImporter.hpp"

namespace Fenrir
{
    namespace
    {
        // The words log messages use for each kind, in AssetKind's order.
        constexpr std::array<std::string_view, Detail::AssetKindCount> kindNames = {
            "mesh", "image", "material", "shader", "model", "skeleton", "clip", "bone mask"};

        std::string_view KindName(Detail::AssetKind kind)
        {
            return kindNames[static_cast<std::size_t>(kind)];
        }

        Detail::AssetTable& TableOf(Detail::AssetStoreState& state, Detail::AssetKind kind)
        {
            return state.tables[static_cast<std::size_t>(kind)];
        }

        // The directory the running executable is in, or the working directory if it cannot be found.
        std::filesystem::path ExecutableDirectory()
        {
#if defined(_WIN32)
            std::wstring buffer(1024, L'\0');
            while (true)
            {
                const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return std::filesystem::current_path();
                }
                if (length < buffer.size())
                {
                    buffer.resize(length);
                    return std::filesystem::path(buffer).parent_path();
                }
                buffer.resize(buffer.size() * 2);
            }
#elif defined(__APPLE__)
            std::uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string buffer(size, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            {
                return std::filesystem::current_path();
            }
            return std::filesystem::weakly_canonical(buffer.c_str()).parent_path();
#else
            std::error_code error;
            const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
            return error ? std::filesystem::current_path() : path.parent_path();
#endif
        }

        std::filesystem::path Utf8Path(std::string_view text)
        {
            return std::filesystem::path(
                std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        std::string Utf8Text(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return {reinterpret_cast<const char*>(text.data()), text.size()};
        }

        // A load request taken apart: the file resolved against the root, its label, and the key both make.
        struct AssetPath
        {
            std::filesystem::path file;
            std::string label;
            bool labelled = false;
            std::string key;
        };

        AssetPath Resolve(const std::filesystem::path& root, std::string_view path)
        {
            AssetPath resolved;
            const std::size_t hash = path.rfind('#');
            if (hash != std::string_view::npos)
            {
                resolved.label = std::string(path.substr(hash + 1));
                resolved.labelled = true;
                path = path.substr(0, hash);
            }
            const std::filesystem::path file = Utf8Path(path);
            resolved.file = (file.is_absolute() ? file : root / file).lexically_normal();
            resolved.key = Utf8Text(resolved.file);
            if (resolved.labelled)
            {
                resolved.key += '#' + resolved.label;
            }
            return resolved;
        }

        // The prefix of a built-in asset's name, which is its key rather than a path.
        constexpr std::string_view BuiltinPrefix = "builtin:";

        std::string_view ReasonText(LoadFailure reason)
        {
            if (reason == LoadFailure::FileNotFound)
            {
                return "file not found";
            }
            if (reason == LoadFailure::ImportFailed)
            {
                return "import failed";
            }
            if (reason == LoadFailure::LabelNotFound)
            {
                return "label not found";
            }
            if (reason == LoadFailure::NotRegistered)
            {
                return "not registered";
            }
            return "no importer";
        }
    } // namespace

    namespace Detail
    {
        bool ReadAssetFile(const std::filesystem::path& file, std::vector<std::byte>& bytes)
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                return false;
            }
            const std::vector<char> chars((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            if (stream.bad())
            {
                return false;
            }
            bytes.resize(chars.size());
            std::transform(chars.begin(), chars.end(), bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
            return true;
        }

        AssetSlot InsertAsset(AssetTable& table, AssetBox data, OwnerId owner, std::string key)
        {
            std::uint32_t index = 0;
            if (!table.freeIndices.empty())
            {
                index = table.freeIndices.back();
                table.freeIndices.pop_back();
            }
            else
            {
                index = static_cast<std::uint32_t>(table.records.size());
                table.records.emplace_back();
            }
            AssetRecord& record = table.records[index];
            record.data = std::move(data);
            record.revision = 1;
            record.owners = {owner};
            record.key = std::move(key);
            if (!record.key.empty())
            {
                table.byKey.emplace(record.key, index);
            }
            return {index, record.generation};
        }

        void AddOwner(AssetRecord& record, OwnerId owner)
        {
            if (std::find(record.owners.begin(), record.owners.end(), owner) == record.owners.end())
            {
                record.owners.push_back(owner);
            }
        }

        OwnerId OwnerIdOf(const std::shared_ptr<AssetStoreState>& state, AssetOwner owner)
        {
            Scene* scene = owner.GetScene();
            if (scene == nullptr)
            {
                return AppOwnerId;
            }
            // A scene that owned assets of an earlier store keeps that store's resource until it is replaced here.
            if (const auto* existing = scene->GetResource<SceneAssetOwner>();
                existing != nullptr && existing->store.lock() == state)
            {
                return existing->id;
            }
            const OwnerId id = state->nextSceneOwner++;
            scene->AddResource<SceneAssetOwner>(state, id);
            return id;
        }

        AssetRecord* AssetStoreState::Find(AssetKind kind, AssetSlot slot)
        {
            AssetTable& table = TableOf(*this, kind);
            if (slot.generation == 0 || slot.index >= table.records.size())
            {
                return nullptr;
            }
            AssetRecord& record = table.records[slot.index];
            return record.revision != 0 && record.generation == slot.generation ? &record : nullptr;
        }

        void AssetStoreState::Free(AssetKind kind, std::uint32_t index)
        {
            AssetTable& table = TableOf(*this, kind);
            AssetRecord& record = table.records[index];
            record.data.reset();
            record.revision = 0;
            record.owners.clear();
            if (!record.key.empty())
            {
                table.byKey.erase(record.key);
                record.key.clear();
            }
            // Generation zero is the empty handle's, so a slot that wraps around skips it.
            if (++record.generation == 0)
            {
                record.generation = 1;
            }
            table.freeIndices.push_back(index);
        }

        void AssetStoreState::ReleaseOwner(OwnerId owner)
        {
            for (std::size_t kind = 0; kind < AssetKindCount; ++kind)
            {
                std::vector<AssetRecord>& records = tables[kind].records;
                for (std::uint32_t index = 0; index < records.size(); ++index)
                {
                    std::vector<OwnerId>& owners = records[index].owners;
                    const auto found = std::find(owners.begin(), owners.end(), owner);
                    if (found == owners.end())
                    {
                        continue;
                    }
                    owners.erase(found);
                    if (owners.empty())
                    {
                        Free(static_cast<AssetKind>(kind), index);
                    }
                }
            }
        }
    } // namespace Detail

    AssetStore::AssetStore(ILogger& logger, AssetsConfig config) : m_state(std::make_shared<Detail::AssetStoreState>())
    {
        m_state->logger = &logger;
        const std::filesystem::path root =
            config.root.empty() ? ExecutableDirectory() / "assets" : std::filesystem::absolute(config.root);
        m_state->root = root.lexically_normal();
    }

    AssetStore::~AssetStore() = default;

    const std::filesystem::path& AssetStore::Root() const noexcept
    {
        return m_state->root;
    }

    ILogger& AssetStore::Logger() const noexcept
    {
        return *m_state->logger;
    }

    Detail::AssetSlot AssetStore::LoadSlot(Detail::AssetKind kind, std::string_view path, AssetOwner owner,
                                           std::optional<LoadDiagnostic>& diagnostic,
                                           std::vector<std::string>& failedUnits)
    {
        const bool builtin = path.starts_with(BuiltinPrefix);
        const AssetPath resolved = builtin ? AssetPath{{}, {}, false, std::string(path)} : Resolve(m_state->root, path);
        Detail::AssetTable& table = TableOf(*m_state, kind);
        // Every load of a model file's assets reports what its import left out.
        if (const auto failed = m_state->modelFailures.find(resolved.key.substr(0, resolved.key.find('#')));
            !builtin && failed != m_state->modelFailures.end())
        {
            failedUnits = failed->second;
        }
        // A built-in name an import reserved, with no data registered yet, is not loaded.
        if (const auto loaded = table.byKey.find(resolved.key);
            loaded != table.byKey.end() && table.records[loaded->second].data != nullptr)
        {
            Detail::AssetRecord& record = table.records[loaded->second];
            const Detail::OwnerId ownerId = Detail::OwnerIdOf(m_state, owner);
            Detail::AddOwner(record, ownerId);
            if (kind == Detail::AssetKind::Model)
            {
                ShareModel(*static_cast<const Model*>(record.data.get()), ownerId);
            }
            return {loaded->second, record.generation};
        }

        const auto fail = [&](LoadFailure reason, std::string message) -> Detail::AssetSlot {
            m_state->logger->Error("Could not load {} '{}': {} ({})", KindName(kind), resolved.key, ReasonText(reason),
                                   message);
            diagnostic = LoadDiagnostic{resolved.key, reason, std::move(message)};
            failedUnits.clear();
            return {};
        };

        if (builtin)
        {
            return fail(LoadFailure::NotRegistered,
                        "nothing is registered under this built-in name yet (FenrirRenderer registers its built-in "
                        "shaders when it is installed)");
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(resolved.file, error))
        {
            return fail(LoadFailure::FileNotFound, "there is no file at this path");
        }
        if (Detail::IsModelFile(resolved.file))
        {
            if (kind == Detail::AssetKind::Shader)
            {
                return fail(LoadFailure::NoImporter, "a model file holds no shaders");
            }
            if (kind == Detail::AssetKind::BoneMask)
            {
                return fail(LoadFailure::NoImporter, "a model file holds no bone masks");
            }
            if (kind == Detail::AssetKind::Model && resolved.labelled)
            {
                return fail(LoadFailure::LabelNotFound,
                            "a model's sub-assets are meshes, materials, images, skeletons and clips, so '" +
                                resolved.label + "' names no model");
            }
            if (kind != Detail::AssetKind::Model && !resolved.labelled)
            {
                return fail(LoadFailure::LabelNotFound, "a model file's meshes, materials, images, skeletons and clips "
                                                        "load by label, such as 'file#Mesh0'");
            }
            const std::string fileKey = Utf8Text(resolved.file);
            if (!TableOf(*m_state, Detail::AssetKind::Model).byKey.contains(fileKey))
            {
                if (std::string problem = ImportModelFile(resolved.file, fileKey, owner); !problem.empty())
                {
                    return fail(LoadFailure::ImportFailed, std::move(problem));
                }
                if (const auto failed = m_state->modelFailures.find(fileKey); failed != m_state->modelFailures.end())
                {
                    failedUnits = failed->second;
                }
            }
            const auto found = table.byKey.find(resolved.key);
            if (found == table.byKey.end())
            {
                return fail(LoadFailure::LabelNotFound,
                            std::format("the model has no {} labelled '{}'", KindName(kind), resolved.label));
            }
            Detail::AssetRecord& record = table.records[found->second];
            Detail::AddOwner(record, Detail::OwnerIdOf(m_state, owner));
            return {found->second, record.generation};
        }
        if (kind != Detail::AssetKind::Image)
        {
            return fail(LoadFailure::NoImporter,
                        std::string("nothing imports a ") + std::string(KindName(kind)) + " from this kind of file");
        }
        if (resolved.labelled)
        {
            return fail(LoadFailure::LabelNotFound,
                        "an image file has no sub-asset called '" + resolved.label + "', or any other");
        }
        std::vector<std::byte> bytes;
        if (!Detail::ReadAssetFile(resolved.file, bytes))
        {
            return fail(LoadFailure::ImportFailed, "the file could not be read");
        }
        Detail::ImageImport imported = Detail::IsCubemapFile(resolved.file)
                                           ? Detail::ImportCubemapFile(resolved.file, bytes)
                                           : Detail::ImportImage(bytes);
        if (!imported.image)
        {
            return fail(LoadFailure::ImportFailed, std::move(imported.error));
        }
        Detail::AssetWarnings warnings;
        if (std::string problem = Detail::PrepareAsset(*imported.image, warnings); !problem.empty())
        {
            return fail(LoadFailure::ImportFailed, std::move(problem));
        }
        Detail::AssetBox data(new ImageData(std::move(*imported.image)), &Detail::DestroyAssetData<ImageData>);
        return Detail::InsertAsset(table, std::move(data), Detail::OwnerIdOf(m_state, owner), resolved.key);
    }

    Detail::AssetSlot AssetStore::AddSlot(Detail::AssetKind kind, Detail::AssetBox data, AssetOwner owner)
    {
        return Detail::InsertAsset(TableOf(*m_state, kind), std::move(data), Detail::OwnerIdOf(m_state, owner), {});
    }

    Detail::AssetSlot AssetStore::AddBuiltinSlot(Detail::AssetKind kind, std::string_view name, Detail::AssetBox data)
    {
        if (!name.starts_with(BuiltinPrefix))
        {
            Refuse(KindName(kind), "add a built-in",
                   std::format("its name '{}' does not start with '{}'", name, BuiltinPrefix));
            return {};
        }
        const Detail::AssetSlot slot = ReserveBuiltin(kind, name);
        Detail::AssetRecord& record = *m_state->Find(kind, slot);
        const bool replacing = record.data != nullptr;
        record.data = std::move(data);
        if (replacing)
        {
            ++record.revision;
            if (kind == Detail::AssetKind::Shader)
            {
                WarnOverridesShaderLacks({slot.index, slot.generation});
            }
        }
        return slot;
    }

    Detail::AssetSlot AssetStore::ReserveBuiltin(Detail::AssetKind kind, std::string_view name)
    {
        Detail::AssetTable& table = TableOf(*m_state, kind);
        if (const auto found = table.byKey.find(std::string(name)); found != table.byKey.end())
        {
            return {found->second, table.records[found->second].generation};
        }
        // No data yet, so the handle does not resolve until AddBuiltin fills the slot. The empty box never calls its
        // deleter; AddBuiltin's data brings its own.
        return Detail::InsertAsset(table, Detail::AssetBox(nullptr, &Detail::DestroyAssetData<ShaderData>),
                                   Detail::AppOwnerId, std::string(name));
    }

    void AssetStore::ShareModel(const Model& model, std::uint32_t owner)
    {
        const auto share = [&](Detail::AssetKind kind, std::uint32_t index, std::uint32_t generation) {
            if (Detail::AssetRecord* record = m_state->Find(kind, {index, generation}))
            {
                Detail::AddOwner(*record, owner);
            }
        };
        for (const AssetHandle<Mesh> mesh : model.meshes)
        {
            share(Detail::AssetKind::Mesh, mesh.index, mesh.generation);
        }
        for (const AssetHandle<Material> material : model.materials)
        {
            share(Detail::AssetKind::Material, material.index, material.generation);
        }
        for (const AssetHandle<Image> image : model.images)
        {
            share(Detail::AssetKind::Image, image.index, image.generation);
        }
        for (const AssetHandle<Skeleton> skeleton : model.skeletons)
        {
            share(Detail::AssetKind::Skeleton, skeleton.index, skeleton.generation);
        }
        for (const AssetHandle<Clip> clip : model.clips)
        {
            share(Detail::AssetKind::Clip, clip.index, clip.generation);
        }
    }

    bool AssetStore::ReplaceSlot(Detail::AssetKind kind, Detail::AssetSlot slot, Detail::AssetBox data)
    {
        Detail::AssetRecord* record = m_state->Find(kind, slot);
        if (record == nullptr)
        {
            Refuse(KindName(kind), "replace", "its handle is empty or stale");
            return false;
        }
        record->data = std::move(data);
        ++record->revision;
        if (kind == Detail::AssetKind::Shader)
        {
            WarnOverridesShaderLacks({slot.index, slot.generation});
        }
        return true;
    }

    bool AssetStore::UnloadSlot(Detail::AssetKind kind, Detail::AssetSlot slot)
    {
        if (m_state->Find(kind, slot) == nullptr)
        {
            return false;
        }
        m_state->Free(kind, slot.index);
        return true;
    }

    const void* AssetStore::Find(Detail::AssetKind kind, Detail::AssetSlot slot) const
    {
        const Detail::AssetRecord* record = m_state->Find(kind, slot);
        return record != nullptr ? record->data.get() : nullptr;
    }

    std::span<const ShaderParameter> AssetStore::GetShaderParameters(AssetHandle<Shader> handle) const
    {
        const ShaderData* shader = TryGet(handle);
        return shader != nullptr ? std::span<const ShaderParameter>(shader->parameters)
                                 : std::span<const ShaderParameter>();
    }

    std::uint32_t AssetStore::RevisionOf(Detail::AssetKind kind, Detail::AssetSlot slot) const
    {
        const Detail::AssetRecord* record = m_state->Find(kind, slot);
        return record != nullptr && record->data != nullptr ? record->revision : 0;
    }

    bool AssetStore::SameFileOf(Detail::AssetKind kindA, Detail::AssetSlot a, Detail::AssetKind kindB,
                                Detail::AssetSlot b) const
    {
        const Detail::AssetRecord* first = m_state->Find(kindA, a);
        const Detail::AssetRecord* second = m_state->Find(kindB, b);
        if (first == nullptr || second == nullptr || first->data == nullptr || second->data == nullptr)
        {
            return false;
        }
        // A loaded asset's key is its file, then `#` and its label for a sub-asset; an added one has none, and a
        // built-in's is its name, not a file.
        const auto fileOf = [](const std::string& key) { return std::string_view(key).substr(0, key.find('#')); };
        if (first->key.empty() || second->key.empty() || first->key.starts_with(BuiltinPrefix) ||
            second->key.starts_with(BuiltinPrefix))
        {
            return false;
        }
        return fileOf(first->key) == fileOf(second->key);
    }

    void AssetStore::Refuse(std::string_view assetName, std::string_view action, const std::string& problem)
    {
        m_state->logger->Error("Could not {} a {}: {}", action, assetName, problem);
    }

    void AssetStore::Warn(std::string_view assetName, std::string_view action, const Detail::AssetWarnings& warnings)
    {
        for (const std::string& warning : warnings)
        {
            m_state->logger->Warn("A {} {}: {}", assetName, action, warning);
        }
    }

    Detail::AssetRecord* AssetStore::EditRecord(Detail::AssetKind kind, Detail::AssetSlot slot, std::string_view action)
    {
        Detail::AssetRecord* record = m_state->Find(kind, slot);
        if (record == nullptr || record->data == nullptr)
        {
            m_state->logger->Error("Could not {}: the {} handle is empty or stale", action, KindName(kind));
            return nullptr;
        }
        return record;
    }
} // namespace Fenrir
