#include "FenrirAssets/AssetStore.hpp"

#include <algorithm>
#include <cstddef>
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
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "Importers/ImageImporter.hpp"

namespace Fenrir
{
    namespace
    {
        // The words log messages use for each kind, in AssetKind's order.
        constexpr std::array<std::string_view, Detail::AssetKindCount> kindNames = {"mesh", "image", "material",
                                                                                    "shader", "model"};

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

        bool ReadFile(const std::filesystem::path& file, std::vector<std::byte>& bytes)
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
            return "no importer";
        }

        Detail::OwnerId OwnerIdOf(const std::shared_ptr<Detail::AssetStoreState>& state, AssetOwner owner)
        {
            Scene* scene = owner.GetScene();
            if (scene == nullptr)
            {
                return Detail::AppOwnerId;
            }
            // A scene that owned assets of an earlier store keeps that store's resource until it is replaced here.
            if (const auto* existing = scene->GetResource<Detail::SceneAssetOwner>();
                existing != nullptr && existing->store.lock() == state)
            {
                return existing->id;
            }
            const Detail::OwnerId id = state->nextSceneOwner++;
            scene->AddResource<Detail::SceneAssetOwner>(state, id);
            return id;
        }

        void AddOwner(Detail::AssetRecord& record, Detail::OwnerId owner)
        {
            if (std::find(record.owners.begin(), record.owners.end(), owner) == record.owners.end())
            {
                record.owners.push_back(owner);
            }
        }

        Detail::AssetSlot Insert(Detail::AssetTable& table, Detail::AssetBox data, Detail::OwnerId owner,
                                 std::string key)
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
            Detail::AssetRecord& record = table.records[index];
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
    } // namespace

    namespace Detail
    {
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

    Detail::AssetSlot AssetStore::LoadSlot(Detail::AssetKind kind, std::string_view path, AssetOwner owner,
                                           std::optional<LoadDiagnostic>& diagnostic)
    {
        const AssetPath resolved = Resolve(m_state->root, path);
        Detail::AssetTable& table = TableOf(*m_state, kind);
        if (const auto loaded = table.byKey.find(resolved.key); loaded != table.byKey.end())
        {
            Detail::AssetRecord& record = table.records[loaded->second];
            AddOwner(record, OwnerIdOf(m_state, owner));
            return {loaded->second, record.generation};
        }

        const auto fail = [&](LoadFailure reason, std::string message) -> Detail::AssetSlot {
            m_state->logger->Error("Could not load {} '{}': {} ({})", KindName(kind), resolved.key, ReasonText(reason),
                                   message);
            diagnostic = LoadDiagnostic{resolved.key, reason, std::move(message)};
            return {};
        };

        std::error_code error;
        if (!std::filesystem::is_regular_file(resolved.file, error))
        {
            return fail(LoadFailure::FileNotFound, "there is no file at this path");
        }
        if (kind != Detail::AssetKind::Image)
        {
            return fail(LoadFailure::NoImporter,
                        std::string("nothing imports a ") + std::string(KindName(kind)) + " from a file yet");
        }
        if (resolved.labelled)
        {
            return fail(LoadFailure::LabelNotFound,
                        "an image file has no sub-asset called '" + resolved.label + "', or any other");
        }
        std::vector<std::byte> bytes;
        if (!ReadFile(resolved.file, bytes))
        {
            return fail(LoadFailure::ImportFailed, "the file could not be read");
        }
        Detail::ImageImport imported = Detail::ImportImage(bytes);
        if (!imported.image)
        {
            return fail(LoadFailure::ImportFailed, std::move(imported.error));
        }
        if (std::string problem = Detail::PrepareAsset(*imported.image); !problem.empty())
        {
            return fail(LoadFailure::ImportFailed, std::move(problem));
        }
        Detail::AssetBox data(new ImageData(std::move(*imported.image)), &Detail::DestroyAssetData<ImageData>);
        return Insert(table, std::move(data), OwnerIdOf(m_state, owner), resolved.key);
    }

    Detail::AssetSlot AssetStore::AddSlot(Detail::AssetKind kind, Detail::AssetBox data, AssetOwner owner)
    {
        return Insert(TableOf(*m_state, kind), std::move(data), OwnerIdOf(m_state, owner), {});
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
        return record != nullptr ? record->revision : 0;
    }

    void AssetStore::Refuse(std::string_view assetName, std::string_view action, const std::string& problem)
    {
        m_state->logger->Error("Could not {} a {}: {}", action, assetName, problem);
    }
} // namespace Fenrir
