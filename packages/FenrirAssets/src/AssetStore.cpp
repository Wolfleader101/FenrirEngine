#include "FenrirAssets/AssetStore.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
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
#include "AssetWatcher.hpp"
#include "CubemapLoading.hpp"
#include "Envelope.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "Importers/ImageImporter.hpp"
#include "Importers/ModelImporter.hpp"

namespace Fenrir
{
    namespace
    {
        using Detail::BuiltinPrefix;
        using Detail::RootRelative;
        using Detail::Utf8Path;
        using Detail::Utf8Text;
        using Detail::WriteAtomically;

        // How each kind is named, in AssetKind's order: the word log messages use, and the target a description
        // names its handle by (`AssetHandle<Image>`'s is `Fenrir::Image`). A data asset's target is its own type's.
        struct KindNames
        {
            std::string_view word;
            std::string_view target;
        };
        constexpr std::array<KindNames, Detail::AssetKindCount> kindNames = {{
            {"mesh", "Fenrir::Mesh"},
            {"image", "Fenrir::Image"},
            {"material", "Fenrir::Material"},
            {"shader", "Fenrir::Shader"},
            {"model", "Fenrir::Model"},
            {"skeleton", "Fenrir::Skeleton"},
            {"clip", "Fenrir::Clip"},
            {"bone mask", "Fenrir::BoneMask"},
            {"instance list", "Fenrir::InstanceList"},
            {"font", "Fenrir::Font"},
            {"data asset", {}},
        }};

        std::string_view KindName(Detail::AssetKind kind)
        {
            return kindNames[static_cast<std::size_t>(kind)].word;
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

        // A load request taken apart: the file resolved against the root, its label, and the key both make.
        struct AssetPath
        {
            std::filesystem::path file;
            std::string label;
            bool labelled = false;
            std::string key;
        };

        // Whether `file` is named as a TrueType or OpenType font is, by its extension in any case.
        bool IsFontFile(const std::filesystem::path& file)
        {
            std::string extension = file.extension().string();
            std::ranges::transform(extension, extension.begin(), [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            });
            return extension == ".ttf" || extension == ".otf" || extension == ".ttc";
        }

        // Whether `file` is named as a shader container FenrirShaderCompiler writes is, `.fshader` in any case.
        bool IsShaderContainerFile(const std::filesystem::path& file)
        {
            std::string extension = file.extension().string();
            std::ranges::transform(extension, extension.begin(), [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            });
            return extension == ".fshader";
        }

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

        // Why a data asset of `type` is refused when the store's type registry lacks it.
        std::string UnregisteredType(const TypeDescription& type)
        {
            return std::format("the type registry has no `{}`: register its package's types first", type.name);
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
            if (reason == LoadFailure::NotRegistered)
            {
                return "not registered";
            }
            if (reason == LoadFailure::NotADataAsset)
            {
                return "not a data asset";
            }
            if (reason == LoadFailure::WrongType)
            {
                return "wrong type";
            }
            if (reason == LoadFailure::OutsideRoot)
            {
                return "outside the asset root";
            }
            return "no importer";
        }

        // Whether an asset of `table` is from the file `fileKey` names: a data asset's top level or sub-asset, a model
        // or a model's sub-asset, or an image or font file's one asset.
        bool FileInUse(const Detail::AssetTable& table, const std::string& fileKey)
        {
            const std::string prefix = fileKey + '#';
            return table.byKey.contains(fileKey) ||
                   std::ranges::any_of(table.byKey, [&](const auto& keyed) { return keyed.first.starts_with(prefix); });
        }

        // The one asset an image, cubemap, font or shader container file imports to, into `data`, as a load and a
        // reload both import it; or why it imports to none.
        std::string ImportFileAsset(Detail::AssetKind kind, const std::filesystem::path& file, Detail::AssetBox& data)
        {
            Detail::AssetWarnings warnings;
            if (kind == Detail::AssetKind::Shader)
            {
                ShaderData shader;
                if (!Detail::ReadAssetFile(file, shader.bytes))
                {
                    return "the file could not be read";
                }
                if (std::string problem = Detail::PrepareAsset(shader, warnings); !problem.empty())
                {
                    return problem;
                }
                data = Detail::AssetBox(new ShaderData(std::move(shader)), &Detail::DestroyAssetData<ShaderData>);
                return {};
            }
            if (kind == Detail::AssetKind::Font)
            {
                FontData font;
                if (!Detail::ReadAssetFile(file, font.bytes))
                {
                    return "the file could not be read";
                }
                if (std::string problem = Detail::PrepareAsset(font, warnings); !problem.empty())
                {
                    return problem;
                }
                data = Detail::AssetBox(new FontData(std::move(font)), &Detail::DestroyAssetData<FontData>);
                return {};
            }
            std::vector<std::byte> bytes;
            if (!Detail::ReadAssetFile(file, bytes))
            {
                return "the file could not be read";
            }
            Detail::ImageImport imported =
                Detail::IsCubemapFile(file) ? Detail::ImportCubemapFile(file, bytes) : Detail::ImportImage(bytes);
            if (!imported.image)
            {
                return std::move(imported.error);
            }
            if (std::string problem = Detail::PrepareAsset(*imported.image, warnings); !problem.empty())
            {
                return problem;
            }
            data = Detail::AssetBox(new ImageData(std::move(*imported.image)), &Detail::DestroyAssetData<ImageData>);
            return {};
        }

        // Logs that the changed file `path` did not reload as a `kind`, and why: its assets keep what they loaded.
        void LogKept(ILogger& logger, Detail::AssetKind kind, std::string_view path, std::string_view problem)
        {
            logger.Error("Could not reload {} '{}': {}; what it loaded is kept", KindName(kind), path, problem);
        }

    } // namespace

    namespace Detail
    {
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

        std::optional<std::string> RootRelative(const std::filesystem::path& root, const std::filesystem::path& file)
        {
            const std::filesystem::path relative = file.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() || relative.has_root_name() || *relative.begin() == "..")
            {
                return std::nullopt;
            }
            return Utf8Text(relative);
        }

        std::optional<std::string> WriteAtomically(const std::filesystem::path& file, std::string_view text)
        {
            std::error_code error;
            std::filesystem::create_directories(file.parent_path(), error);
            if (error)
            {
                return std::format("its directory could not be made ({})", error.message());
            }
            std::filesystem::path beside = file;
            beside += ".saving";
            {
                std::ofstream stream(beside, std::ios::binary | std::ios::trunc);
                if (!stream)
                {
                    return std::format("'{}', written before the rename, could not be opened", Utf8Text(beside));
                }
                stream.write(text.data(), static_cast<std::streamsize>(text.size()));
                stream.close();
                if (!stream)
                {
                    std::filesystem::remove(beside, error);
                    return std::format("'{}', written before the rename, could not be written", Utf8Text(beside));
                }
            }
            std::filesystem::rename(beside, file, error);
            if (error)
            {
                const std::string message = error.message();
                std::filesystem::remove(beside, error);
                return std::format("'{}' could not be renamed over the file ({})", Utf8Text(beside), message);
            }
            return std::nullopt;
        }

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
            return const_cast<AssetRecord*>(std::as_const(*this).Find(kind, slot));
        }

        const AssetRecord* AssetStoreState::Find(AssetKind kind, AssetSlot slot) const
        {
            const AssetTable& table = tables[static_cast<std::size_t>(kind)];
            if (slot.generation == 0 || slot.index >= table.records.size())
            {
                return nullptr;
            }
            const AssetRecord& record = table.records[slot.index];
            return record.revision != 0 && record.generation == slot.generation ? &record : nullptr;
        }

        AssetStoreState::AssetStoreState() = default;
        AssetStoreState::~AssetStoreState() = default;

        void AssetStoreState::Free(AssetKind kind, std::uint32_t index)
        {
            AssetTable& table = TableOf(*this, kind);
            AssetRecord& record = table.records[index];
            record.data.reset();
            record.revision = 0;
            record.owners.clear();
            record.imageUpdates.clear();
            if (!record.key.empty())
            {
                table.byKey.erase(record.key);
                // A data asset file's tree, and its watch, go with the last record from the file, its top level's or a
                // sub-asset's.
                if (kind == AssetKind::Data)
                {
                    const std::string file = record.key.substr(0, record.key.find('#'));
                    if (!FileInUse(table, file))
                    {
                        fileTrees.erase(file);
                        if (watcher)
                        {
                            watcher->Forget(file);
                        }
                    }
                }
                // An imported file's watch goes with the last asset from it of any kind: a model file's sub-assets
                // live on after its model, and an image or font file is its one asset.
                else if (watcher && !record.key.starts_with(BuiltinPrefix))
                {
                    const std::string file = record.key.substr(0, record.key.find('#'));
                    if (watcher->Watches(file) &&
                        std::ranges::none_of(tables, [&](const AssetTable& held) { return FileInUse(held, file); }))
                    {
                        watcher->Forget(file);
                    }
                }
                record.key.clear();
            }
            record.typeName = {};
            record.firstTypeName = {};
            record.loadDiagnostics.clear();
            record.loadIncomplete = false;
            record.references.clear();
            record.logged.clear();
            // Generation zero is the empty handle's, so a slot that wraps around skips it.
            if (++record.generation == 0)
            {
                record.generation = 1;
            }
            table.freeIndices.push_back(index);
        }

        std::optional<AssetKind> KindOfTarget(std::string_view target)
        {
            const auto found = std::ranges::find_if(
                kindNames, [&](const KindNames& names) { return !names.target.empty() && names.target == target; });
            if (found == kindNames.end())
            {
                return std::nullopt;
            }
            return static_cast<AssetKind>(std::distance(kindNames.begin(), found));
        }

        std::optional<std::string> PathOf(const AssetStoreState& state, AssetKind kind, AssetSlot slot)
        {
            const AssetRecord* record = state.Find(kind, slot);
            if (record == nullptr || record->key.empty())
            {
                return std::nullopt;
            }
            // A built-in's key is its name, which a material names before the renderer registers its data.
            if (record->key.starts_with(BuiltinPrefix))
            {
                return record->key;
            }
            // A loaded asset's key is its file resolved against the root, then `#` and its label for a sub-asset.
            if (record->data == nullptr)
            {
                return std::nullopt;
            }
            const std::size_t hash = record->key.find('#');
            const std::string_view file = std::string_view(record->key).substr(0, hash);
            const std::string_view label =
                hash == std::string::npos ? std::string_view() : std::string_view(record->key).substr(hash);
            const std::optional<std::string> relative = RootRelative(state.root, Utf8Path(file));
            return (relative ? *relative : std::string(file)) + std::string(label);
        }

        Serialisation::Resolvers WritingResolvers(const AssetStoreState& state)
        {
            Serialisation::Resolvers resolvers;
            resolvers.writeReference = [&state](ReferenceValue reference, const FieldInfo& field) -> std::string {
                const AssetKind kind = KindOfTarget(field.type.target).value_or(AssetKind::Data);
                return PathOf(state, kind, {reference.index, reference.generation}).value_or(std::string());
            };
            return resolvers;
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

    AssetStore::AssetStore(ILogger& logger, AssetsConfig config, const TypeRegistry& types)
        : m_state(std::make_shared<Detail::AssetStoreState>())
    {
        m_state->logger = &logger;
        m_state->types = &types;
        const std::filesystem::path root =
            config.root.empty() ? ExecutableDirectory() / "assets" : std::filesystem::absolute(config.root);
        m_state->root = root.lexically_normal();
        m_state->source = Detail::MakeDirectorySource(m_state->root, logger);
        if (config.hotReload)
        {
            using std::chrono::milliseconds;
            m_state->watcher = std::make_unique<Detail::AssetWatcher>(
                ReloadWatcher::Settings{true, milliseconds(std::max<std::int64_t>(config.reloadInterval, 0)),
                                        milliseconds(std::max<std::int64_t>(config.reloadDebounce, 0))});
        }
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

    std::uint32_t AssetStore::OwnerOf(AssetOwner owner)
    {
        return Detail::OwnerIdOf(m_state, owner);
    }

    Detail::AssetSlot AssetStore::LoadSlot(Detail::AssetKind kind, std::string_view path, std::uint32_t owner,
                                           std::optional<LoadDiagnostic>& diagnostic,
                                           std::vector<std::string>& failedUnits)
    {
        const bool quiet = std::exchange(m_state->quietNextLoad, false);
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
            Detail::AddOwner(record, owner);
            if (kind == Detail::AssetKind::Model)
            {
                ShareModel(*static_cast<const Model*>(record.data.get()), owner);
            }
            return {loaded->second, record.generation};
        }

        const auto fail = [&](LoadFailure reason, std::string message) -> Detail::AssetSlot {
            if (!quiet)
            {
                m_state->logger->Error("Could not load {} '{}': {} ({})", KindName(kind), resolved.key,
                                       ReasonText(reason), message);
            }
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
            if (kind == Detail::AssetKind::InstanceList)
            {
                return fail(LoadFailure::NoImporter, "a model file holds no instance lists");
            }
            if (kind == Detail::AssetKind::Font)
            {
                return fail(LoadFailure::NoImporter, "a model file holds no fonts");
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
            Detail::AddOwner(record, owner);
            return {found->second, record.generation};
        }
        if (kind == Detail::AssetKind::Font)
        {
            if (!IsFontFile(resolved.file))
            {
                return fail(LoadFailure::NoImporter, "fonts load from .ttf, .otf and .ttc files");
            }
            if (resolved.labelled)
            {
                return fail(LoadFailure::LabelNotFound,
                            "a font file has no sub-asset called '" + resolved.label + "', or any other");
            }
        }
        else if (kind == Detail::AssetKind::Shader)
        {
            if (!IsShaderContainerFile(resolved.file))
            {
                return fail(LoadFailure::NoImporter,
                            "shaders load from .fshader files, the shader containers FenrirShaderCompiler writes");
            }
            if (resolved.labelled)
            {
                return fail(LoadFailure::LabelNotFound,
                            "a shader container file has no sub-asset called '" + resolved.label + "', or any other");
            }
        }
        else if (kind != Detail::AssetKind::Image)
        {
            return fail(LoadFailure::NoImporter,
                        std::string("nothing imports a ") + std::string(KindName(kind)) + " from this kind of file");
        }
        else if (resolved.labelled)
        {
            return fail(LoadFailure::LabelNotFound,
                        "an image file has no sub-asset called '" + resolved.label + "', or any other");
        }
        // Looked at before the read, so a write during it is a change the watcher sees.
        const std::optional<ReloadWatcher::Stamp> stamp =
            m_state->watcher ? Detail::StampOf(resolved.file) : std::nullopt;
        Detail::AssetBox data(nullptr, nullptr);
        if (std::string problem = ImportFileAsset(kind, resolved.file, data); !problem.empty())
        {
            return fail(LoadFailure::ImportFailed, std::move(problem));
        }
        const Detail::AssetSlot slot = Detail::InsertAsset(table, std::move(data), owner, resolved.key);
        if (m_state->watcher)
        {
            m_state->watcher->Track(resolved.key, RootRelative(m_state->root, resolved.file).value_or(resolved.key),
                                    kind, stamp, ReloadWatcher::Clock::now());
        }
        return slot;
    }

    Detail::AssetSlot AssetStore::LoadDataSlot(std::string_view path, std::uint32_t owner, const TypeDescription& type,
                                               std::optional<LoadDiagnostic>& diagnostic,
                                               std::vector<Serialisation::Diagnostic>& diagnostics, bool& incomplete)
    {
        const bool quiet = std::exchange(m_state->quietNextLoad, false);
        const AssetPath resolved = Resolve(m_state->root, path);
        const auto fail = [&](LoadFailure reason, std::string message) -> Detail::AssetSlot {
            if (!quiet)
            {
                m_state->logger->Error("Could not load {} '{}' as {}: {} ({})", KindName(Detail::AssetKind::Data),
                                       resolved.key, type.name, ReasonText(reason), message);
            }
            diagnostic = LoadDiagnostic{resolved.key, reason, std::move(message)};
            return {};
        };

        const TypeRegistry& registry = *m_state->types;
        if (registry.Find(type.name) == nullptr)
        {
            return fail(LoadFailure::NotADataAsset, UnregisteredType(type));
        }
        // A data asset's identity is its path under the root (ADR-0076), so one outside it has none.
        const std::optional<std::string> assetPath = RootRelative(m_state->root, resolved.file);
        if (!assetPath)
        {
            return fail(LoadFailure::OutsideRoot,
                        std::format("a data asset loads from under the asset root '{}'", Utf8Text(m_state->root)));
        }

        Detail::AssetTable& table = TableOf(*m_state, Detail::AssetKind::Data);
        if (const auto loaded = table.byKey.find(resolved.key); loaded != table.byKey.end())
        {
            Detail::AssetRecord& record = table.records[loaded->second];
            if (!registry.IsA(record.typeName, type.name))
            {
                return fail(LoadFailure::WrongType, std::format("it is loaded already as a `{}`, which is not a `{}`",
                                                                record.typeName, type.name));
            }
            ShareData(loaded->second, owner);
            // Loading a file's top level loads its sub-assets for the same owner, as a model's are (ADR-0076).
            if (!resolved.labelled)
            {
                const std::string prefix = resolved.key + '#';
                for (const auto& [key, index] : table.byKey)
                {
                    if (key.starts_with(prefix))
                    {
                        ShareData(index, owner);
                    }
                }
            }
            diagnostics = record.loadDiagnostics;
            incomplete = record.loadIncomplete;
            return {loaded->second, record.generation};
        }

        // Looked at before the read, so a write during it is a change the watcher sees.
        const std::optional<ReloadWatcher::Stamp> stamp =
            m_state->watcher ? Detail::StampOf(resolved.file) : std::nullopt;
        Detail::AssetSource& source = *m_state->source;
        const std::optional<std::vector<std::byte>> bytes = source.Read(*assetPath);
        if (!bytes)
        {
            return source.Exists(*assetPath) ? fail(LoadFailure::ImportFailed, "the file could not be read")
                                             : fail(LoadFailure::FileNotFound, "there is no file at this path");
        }

        const std::string fileKey = Utf8Text(resolved.file);
        Detail::EnvelopeRead read = ReadDataFile(
            fileKey, *assetPath, resolved.labelled ? std::optional<std::string_view>(resolved.label) : std::nullopt,
            type, owner, *bytes);
        diagnostics = std::move(read.diagnostics);
        if (read.failure)
        {
            return fail(*read.failure, std::move(read.failureMessage));
        }
        incomplete = read.incomplete;
        // Kept so a save of the file keeps its comments (ADR-0077 §1).
        m_state->fileTrees.insert_or_assign(fileKey, std::move(*read.tree));
        if (m_state->watcher)
        {
            m_state->watcher->Track(fileKey, *assetPath, Detail::AssetKind::Data, stamp, ReloadWatcher::Clock::now());
        }

        // One read gives the file's top level and every sub-asset, each stored under the file's key and its label for
        // the one owner, as a model's sub-assets are, in the slot reserved for it. Each repeats the whole file's
        // diagnostics to a later load, as a model's left-out units are.
        Detail::AssetSlot requested;
        for (std::size_t entry = 0; entry < read.entries.size(); ++entry)
        {
            Detail::EnvelopeEntry& stored = read.entries[entry];
            std::string key = stored.label.empty() ? fileKey : fileKey + '#' + stored.label;
            Detail::AssetSlot slot;
            if (const auto found = table.byKey.find(key); found != table.byKey.end())
            {
                Detail::AssetRecord& record = table.records[found->second];
                slot = {found->second, record.generation};
                if (record.data == nullptr)
                {
                    record.data = std::move(stored.value);
                    record.loadDiagnostics = diagnostics;
                    record.loadIncomplete = incomplete;
                    record.references = std::move(stored.references);
                }
                else
                {
                    ShareData(found->second, owner);
                }
            }
            else
            {
                slot = Detail::InsertAsset(table, std::move(stored.value), owner, std::move(key));
                Detail::AssetRecord& record = *m_state->Find(Detail::AssetKind::Data, slot);
                record.typeName = stored.type->name;
                record.firstTypeName = stored.type->name;
                record.loadDiagnostics = diagnostics;
                record.loadIncomplete = incomplete;
                record.references = std::move(stored.references);
            }
            if (entry == read.requested)
            {
                requested = slot;
            }
        }
        return requested;
    }

    Detail::EnvelopeRead AssetStore::ReadDataFile(const std::string& fileKey, const std::string& assetPath,
                                                  std::optional<std::string_view> label, const TypeDescription& type,
                                                  std::uint32_t owner, std::span<const std::byte> bytes)
    {
        const TypeRegistry& registry = *m_state->types;
        Detail::AssetTable& table = TableOf(*m_state, Detail::AssetKind::Data);
        // What a reference in this file names (ADR-0076 §2, §3): its target, loaded first with this load's owner, of
        // the kind the field holds and, for a data asset, of a type extending the field's; or why it names none.
        const auto readReference = [&](std::string_view reference, const FieldInfo& field) -> Detail::ReferenceRead {
            // A bare `#name` is a sub-asset of the file that names it (ADR-0076).
            const std::string target =
                reference.starts_with('#') ? assetPath + std::string(reference) : std::string(reference);
            const std::string_view targetFile = std::string_view(target).substr(0, target.rfind('#'));
            const auto refuse = [](std::string problem) {
                return Detail::ReferenceRead{Detail::AssetKind::Data, {}, std::move(problem)};
            };
            // The target's load logs nothing of its own: the problem is reported once, at the path in this file.
            std::optional<LoadDiagnostic> failure;
            Detail::AssetSlot slot;
            std::string expected;
            Detail::AssetKind targetKind = Detail::AssetKind::Data;

            if (const std::optional<Detail::AssetKind> kind = Detail::KindOfTarget(field.type.target))
            {
                const std::string_view name = KindName(*kind);
                expected = std::format(
                    "{} {}", std::string_view("aeiou").find(name.front()) != std::string_view::npos ? "an" : "a", name);
                if (targetFile.ends_with(".json5"))
                {
                    return refuse(std::format("expected {}, found a data asset's `.json5` envelope", expected));
                }
                std::vector<std::string> failedUnits;
                targetKind = *kind;
                m_state->quietNextLoad = true;
                slot = LoadSlot(*kind, target, owner, failure, failedUnits);
            }
            else
            {
                const TypeDescription* targetType = registry.Find(field.type.target);
                if (targetType == nullptr || targetType->role != Role::DataAsset)
                {
                    return refuse(std::format("expected `{}`, which the type registry has as no data asset type",
                                              field.type.target));
                }
                expected = std::format("`{}` or a type extending it", targetType->name);
                if (!targetFile.ends_with(".asset.json5"))
                {
                    const std::string extension = Utf8Text(Utf8Path(targetFile).extension());
                    return refuse(std::format("expected {}, found {}, not a data asset's `.asset.json5` envelope",
                                              expected,
                                              extension.empty() ? std::string("a file with no extension")
                                                                : std::format("a `{}` file", extension)));
                }
                // One loaded already, or still loading, is checked by the type it was stored with.
                if (const auto loaded = table.byKey.find(Resolve(m_state->root, target).key);
                    loaded != table.byKey.end() &&
                    !registry.IsA(table.records[loaded->second].typeName, targetType->name))
                {
                    return refuse(
                        std::format("expected {}, found `{}`", expected, table.records[loaded->second].typeName));
                }
                std::vector<Serialisation::Diagnostic> targetDiagnostics;
                bool targetIncomplete = false;
                m_state->quietNextLoad = true;
                slot = LoadDataSlot(target, owner, *targetType, failure, targetDiagnostics, targetIncomplete);
                // A file of the wrong type says what it found in the diagnostic that failed its read, its last.
                if (failure && failure->reason == LoadFailure::WrongType && !targetDiagnostics.empty())
                {
                    return refuse(targetDiagnostics.back().message);
                }
            }

            if (!failure)
            {
                return {targetKind, {slot.index, slot.generation}, {}};
            }
            if (failure->reason == LoadFailure::FileNotFound)
            {
                return refuse(std::format("expected {}, found no file", expected));
            }
            return refuse(std::format("expected {}, found a file that did not load: {} ({})", expected,
                                      ReasonText(failure->reason), failure->importerMessage));
        };

        Detail::EnvelopeHooks hooks;
        // Each entry's slot is reserved under its key, typed but with no data, before any of the file's references
        // load, as ReserveBuiltin reserves a built-in's: a reference back to this file, or to a sub-asset of it, finds
        // the slot through deduplication and gets its handle at once (ADR-0076 §3). One stored already (its file's
        // top level was freed, but it lives on) is shared below instead.
        hooks.reading = [&](std::string_view label, const TypeDescription& entryType) {
            std::string key = label.empty() ? fileKey : fileKey + '#' + std::string(label);
            if (table.byKey.contains(key))
            {
                return;
            }
            const Detail::AssetSlot slot = Detail::InsertAsset(
                table, Detail::AssetBox(nullptr, &Detail::DestroyAssetData<Detail::DataValue>), owner, std::move(key));
            Detail::AssetRecord& reserved = *m_state->Find(Detail::AssetKind::Data, slot);
            reserved.typeName = entryType.name;
            reserved.firstTypeName = entryType.name;
        };
        hooks.readReference = readReference;
        return Detail::ReadDataEnvelope(bytes, fileKey, label, type, registry, hooks, *m_state->logger);
    }

    void AssetStore::ShareData(std::uint32_t index, std::uint32_t owner)
    {
        // Each asset is visited once, so references that form a cycle end.
        std::vector<std::pair<Detail::AssetKind, std::uint32_t>> visited;
        const auto share = [&](const auto& self, Detail::AssetKind kind, Detail::AssetSlot slot) -> void {
            Detail::AssetRecord* record = m_state->Find(kind, slot);
            if (record == nullptr || std::ranges::find(visited, std::pair(kind, slot.index)) != visited.end())
            {
                return;
            }
            visited.emplace_back(kind, slot.index);
            Detail::AddOwner(*record, owner);
            if (kind == Detail::AssetKind::Model && record->data != nullptr)
            {
                ShareModel(*static_cast<const Model*>(record->data.get()), owner);
            }
            for (const auto& [targetKind, target] : record->references)
            {
                self(self, targetKind, target);
            }
        };
        const Detail::AssetRecord& record = TableOf(*m_state, Detail::AssetKind::Data).records[index];
        share(share, Detail::AssetKind::Data, {index, record.generation});
    }

    Detail::AssetSlot AssetStore::AddSlot(Detail::AssetKind kind, Detail::AssetBox data, AssetOwner owner)
    {
        return Detail::InsertAsset(TableOf(*m_state, kind), std::move(data), Detail::OwnerIdOf(m_state, owner), {});
    }

    void* AssetStore::AddDataSlot(const TypeDescription& type, AssetOwner owner, std::optional<std::string_view> name,
                                  Detail::AssetSlot& slot)
    {
        const std::string_view word = KindName(Detail::AssetKind::Data);
        const TypeDescription* registered = m_state->types->Find(type.name);
        if (registered == nullptr)
        {
            Refuse(word, "add", UnregisteredType(type));
            return nullptr;
        }
        Detail::AssetTable& table = TableOf(*m_state, Detail::AssetKind::Data);
        std::string key;
        if (name)
        {
            // Keyed as a load of the name is (ADR-0076), so a later load of it finds this asset.
            const AssetPath resolved = Resolve(m_state->root, *name);
            const std::optional<std::string> assetPath = RootRelative(m_state->root, resolved.file);
            if (!assetPath || *assetPath == ".")
            {
                Refuse(word, "add",
                       std::format("its name '{}' is outside the asset root '{}', so it has no asset path", *name,
                                   Utf8Text(m_state->root)));
                return nullptr;
            }
            if (table.byKey.contains(resolved.key))
            {
                Refuse(word, "add", std::format("its name '{}' names an asset the store has already", *name));
                return nullptr;
            }
            key = resolved.key;
        }
        auto value = std::make_unique<Detail::DataValue>(*registered);
        void* instance = value->value;
        slot =
            Detail::InsertAsset(table, Detail::AssetBox(value.release(), &Detail::DestroyAssetData<Detail::DataValue>),
                                Detail::OwnerIdOf(m_state, owner), std::move(key));
        Detail::AssetRecord& added = *m_state->Find(Detail::AssetKind::Data, slot);
        added.typeName = registered->name;
        added.firstTypeName = registered->name;
        return instance;
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
            record.imageUpdates.clear();
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
        record->imageUpdates.clear();
        if (kind == Detail::AssetKind::Shader)
        {
            WarnOverridesShaderLacks({slot.index, slot.generation});
        }
        return true;
    }

    void* AssetStore::ReplaceData(Detail::AssetSlot slot, const TypeDescription& type)
    {
        const std::string_view word = KindName(Detail::AssetKind::Data);
        // Worded as an engine kind's Replace is (ReplaceSlot).
        Detail::AssetRecord* record =
            EditRecord(Detail::AssetKind::Data, slot, "replace", "Could not {} a {}: its handle is empty or stale");
        if (record == nullptr)
        {
            return nullptr;
        }
        // Exactly its type: a base's value would leave the fields of the type extending it as they were.
        if (record->typeName != type.name)
        {
            Refuse(word, "replace",
                   std::format("it is a `{}`, and only a value of exactly its type replaces it, not a `{}`",
                               record->typeName, type.name));
            return nullptr;
        }
        ++record->revision;
        return static_cast<Detail::DataValue*>(record->data.get())->value;
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

    const void* AssetStore::FindData(Detail::AssetSlot slot, const TypeDescription& type) const
    {
        Detail::AssetRecord* record = m_state->Find(Detail::AssetKind::Data, slot);
        if (record == nullptr || record->data == nullptr)
        {
            return nullptr;
        }
        if (!m_state->types->IsA(record->typeName, type.name))
        {
            // The record's own log, which Free clears, so it is once per handle: a new asset in the slot starts afresh.
            const std::scoped_lock lock(m_state->readLogMutex);
            if (record->logged.insert(std::format("read as {}", type.name)).second)
            {
                m_state->logger->Error("Could not read a data asset as `{}`: its handle names a `{}`", type.name,
                                       record->typeName);
            }
            return nullptr;
        }
        return static_cast<const Detail::DataValue*>(record->data.get())->value;
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

    std::optional<std::string> AssetStore::PathOfSlot(Detail::AssetKind kind, Detail::AssetSlot slot) const
    {
        return Detail::PathOf(*m_state, kind, slot);
    }

    SaveResult AssetStore::SaveData(Detail::AssetSlot slot, const TypeDescription& type,
                                    std::optional<std::string_view> path)
    {
        SaveResult result;
        Detail::AssetTable& table = TableOf(*m_state, Detail::AssetKind::Data);
        Detail::AssetRecord* record = m_state->Find(Detail::AssetKind::Data, slot);
        // The file the save is about, as its diagnostics name it: the path given, then the file resolved.
        std::string named = path ? std::string(*path) : std::string();
        const auto fail = [&](std::string message) -> SaveResult {
            m_state->logger->Error("Could not save {} {} as {}: {}", KindName(Detail::AssetKind::Data),
                                   named.empty() ? std::string("added with no name") : std::format("'{}'", named),
                                   type.name, message);
            result.status = SaveStatus::Failed;
            result.diagnostics.push_back(
                {.severity = Serialisation::DiagnosticSeverity::Error, .path = named, .message = std::move(message)});
            return std::move(result);
        };

        if (record == nullptr || record->data == nullptr)
        {
            return fail("the data asset handle is empty or stale");
        }
        // A key's file is the part before its label; an asset added with no name has neither.
        const std::size_t hash = record->key.find('#');
        const std::string fileKey = record->key.substr(0, hash);
        if (!path)
        {
            named = fileKey;
        }
        if (!m_state->types->IsA(record->typeName, type.name))
        {
            return fail(std::format("it is a `{}`, which is not a `{}`", record->typeName, type.name));
        }

        std::filesystem::path target;
        std::string targetKey;
        if (path)
        {
            if (hash != std::string::npos)
            {
                return fail("it is a sub-asset, which is saved with its whole file, to that file's own path");
            }
            // The writer's path check (ADR-0085 §1): an asset path is relative, under the root, and names a file.
            const std::filesystem::path given = Utf8Path(*path);
            if (given.is_absolute() || given.has_root_name() || given.has_root_directory())
            {
                return fail("the path is absolute: an asset path is relative to the asset root");
            }
            if (path->find('#') != std::string_view::npos)
            {
                return fail("the path names a sub-asset: a save writes a whole file");
            }
            const AssetPath resolved = Resolve(m_state->root, *path);
            const std::optional<std::string> relative = RootRelative(m_state->root, resolved.file);
            if (!relative || *relative == ".")
            {
                return fail(std::format("the path is outside the asset root '{}'", Utf8Text(m_state->root)));
            }
            target = resolved.file;
            targetKey = resolved.key;
            named = targetKey;
            // Another asset's file is the store's to save from that asset, not this one's to write over.
            if (targetKey != fileKey && FileInUse(table, targetKey))
            {
                return fail("the path names the file another asset of the store is from");
            }
        }
        else
        {
            if (fileKey.empty())
            {
                return fail("it has no asset path, since it was added with no name: save it to a path");
            }
            target = Utf8Path(fileKey);
            targetKey = fileKey;
        }

        // What the file holds: for an asset added with no name, itself; otherwise its file's top level and every
        // sub-asset of it the store holds, with the file's tree as the store last read or wrote it.
        const auto valueOf = [](const Detail::AssetRecord& held) {
            return static_cast<const Detail::DataValue*>(held.data.get());
        };
        const Detail::DataValue* top = nullptr;
        std::vector<Detail::EnvelopeSub> subs;
        const Serialisation::Value* prior = nullptr;
        if (fileKey.empty())
        {
            top = valueOf(*record);
        }
        else
        {
            if (const auto found = table.byKey.find(fileKey); found != table.byKey.end())
            {
                top = valueOf(table.records[found->second]);
            }
            const std::string prefix = fileKey + '#';
            for (const auto& [key, index] : table.byKey)
            {
                if (key.starts_with(prefix) && table.records[index].data != nullptr)
                {
                    subs.push_back({std::string_view(key).substr(prefix.size()), valueOf(table.records[index])});
                }
            }
            // The order the file has is the writer's; one it lacks is written in name order, the same on every save.
            std::ranges::sort(subs, {}, &Detail::EnvelopeSub::label);
            if (const auto tree = m_state->fileTrees.find(fileKey); tree != m_state->fileTrees.end())
            {
                prior = &tree->second;
            }
        }
        if (top == nullptr && prior == nullptr)
        {
            return fail("its file's top level is no longer loaded, so the file cannot be written whole");
        }

        Detail::EnvelopeWrite written = Detail::WriteDataEnvelope(
            top, subs, prior, *m_state->types, Detail::WritingResolvers(*m_state), named, *m_state->logger);
        result.diagnostics = std::move(written.diagnostics);
        result.emptyReferences = written.emptyReferences;
        if (const std::optional<std::string> problem = WriteAtomically(target, Serialisation::WriteText(written.tree)))
        {
            return fail(*problem);
        }

        // An asset with no path takes the one it was saved to, keyed as a load of it is (ADR-0076).
        if (record->key.empty())
        {
            record->key = targetKey;
            table.byKey.emplace(targetKey, slot.index);
        }
        // What was written is the file's tree now, for the next save of it; a copy written elsewhere is no file of the
        // store's.
        if (record->key == targetKey || targetKey == fileKey)
        {
            m_state->fileTrees.insert_or_assign(targetKey, std::move(written.tree));
            // The store's own write is no change to reload; a file an added asset was first saved to is watched now.
            if (m_state->watcher)
            {
                m_state->watcher->Track(targetKey, RootRelative(m_state->root, target).value_or(targetKey),
                                        Detail::AssetKind::Data, Detail::StampOf(target), ReloadWatcher::Clock::now());
            }
        }
        result.status = result.emptyReferences > 0 ? SaveStatus::Partial : SaveStatus::Succeeded;
        return result;
    }

    std::span<const ReloadedAsset> AssetStore::Reloaded() const noexcept
    {
        return m_state->reloaded;
    }

    void AssetStore::PollForReload()
    {
        m_state->reloaded.clear();
        if (!m_state->watcher)
        {
            return;
        }
        m_state->watcher->Poll(ReloadWatcher::Clock::now(), *m_state->logger,
                               [this](const std::string& key, Detail::AssetKind kind) {
                                   // A data asset file reloads through its envelope, an imported one through its
                                   // importer (ADR-0081 §2).
                                   if (kind == Detail::AssetKind::Data)
                                   {
                                       ReloadDataFile(key);
                                   }
                                   else if (kind == Detail::AssetKind::Model)
                                   {
                                       ReloadModelFile(key);
                                   }
                                   else
                                   {
                                       ReloadImportedFile(key, kind);
                                   }
                               });
    }

    void AssetStore::ReloadImportedFile(const std::string& fileKey, Detail::AssetKind kind)
    {
        Detail::AssetTable& table = TableOf(*m_state, kind);
        const auto found = table.byKey.find(fileKey);
        if (found == table.byKey.end())
        {
            return;
        }
        const Detail::AssetSlot slot{found->second, table.records[found->second].generation};
        const std::string assetPath = Detail::PathOf(*m_state, kind, slot).value_or(fileKey);
        // Imported into its own box, so a failure leaves the asset as it was.
        Detail::AssetBox data(nullptr, nullptr);
        if (const std::string problem = ImportFileAsset(kind, Utf8Path(fileKey), data); !problem.empty())
        {
            LogKept(*m_state->logger, kind, assetPath, problem);
            return;
        }
        if (ReplaceSlot(kind, slot, std::move(data)))
        {
            m_state->reloaded.push_back({assetPath, kind, slot});
        }
    }

    void AssetStore::ReloadModelFile(const std::string& fileKey)
    {
        if (std::string problem = ImportModelFile(Utf8Path(fileKey), fileKey, Detail::ReloadOwnerId); !problem.empty())
        {
            LogKept(*m_state->logger, Detail::AssetKind::Model,
                    RootRelative(m_state->root, Utf8Path(fileKey)).value_or(fileKey), problem);
        }
    }

    void AssetStore::ReloadDataFile(const std::string& fileKey)
    {
        Detail::AssetTable& table = TableOf(*m_state, Detail::AssetKind::Data);
        const TypeRegistry& registry = *m_state->types;
        const std::string prefix = fileKey + '#';
        // What the store holds from the file, by label: its top level (the empty label, sorted first) and each
        // sub-asset.
        std::vector<std::pair<std::string, std::uint32_t>> held;
        for (const auto& [key, index] : table.byKey)
        {
            if (key == fileKey)
            {
                held.emplace_back(std::string(), index);
            }
            else if (key.starts_with(prefix))
            {
                held.emplace_back(key.substr(prefix.size()), index);
            }
        }
        if (held.empty())
        {
            return;
        }
        std::ranges::sort(held);
        const std::string assetPath = RootRelative(m_state->root, Utf8Path(fileKey)).value_or(fileKey);
        const auto keep = [&](std::string_view problem) {
            LogKept(*m_state->logger, Detail::AssetKind::Data, assetPath, problem);
        };

        // The file is read as the type its first entry the store has first held, so each handle to it reads the new
        // value as the type it was loaded as.
        const std::string askedLabel = held.front().first;
        const TypeDescription* asked = registry.Find(table.records[held.front().second].firstTypeName);
        const std::optional<std::vector<std::byte>> bytes = m_state->source->Read(assetPath);
        if (asked == nullptr || !bytes)
        {
            keep(asked == nullptr ? "its type is no longer in the type registry" : "the file could not be read");
            return;
        }
        Detail::EnvelopeRead read = ReadDataFile(
            fileKey, assetPath, askedLabel.empty() ? std::nullopt : std::optional<std::string_view>(askedLabel), *asked,
            Detail::ReloadOwnerId, *bytes);

        // All or nothing (ADR-0081 §2): a read that failed, or read any value in part, or left out a sub-asset the
        // store holds, or made one a type its handles cannot read, replaces nothing. The reader logged each located
        // diagnostic but the one that failed it, which is logged here.
        const auto entryOf = [&](std::string_view label) {
            return std::ranges::find(read.entries, label, &Detail::EnvelopeEntry::label);
        };
        const auto fileHas = [&](std::string_view label) {
            if (!read.tree || read.tree->Kind() != Serialisation::ValueKind::Object)
            {
                return false;
            }
            const Serialisation::Value* sub = read.tree->AsObject().Find("sub");
            return sub != nullptr && sub->Kind() == Serialisation::ValueKind::Object &&
                   sub->AsObject().Find(label) != nullptr;
        };
        std::string problem;
        if (read.failure)
        {
            problem = read.failureMessage.empty() ? std::string(ReasonText(*read.failure)) : read.failureMessage;
        }
        else if (read.incomplete || std::ranges::any_of(read.diagnostics, [](const Serialisation::Diagnostic& found) {
                     return found.severity == Serialisation::DiagnosticSeverity::Error;
                 }))
        {
            problem = "the file has values that did not read, logged above";
        }
        else
        {
            for (const auto& [label, index] : held)
            {
                const auto entry = entryOf(label);
                const std::string_view was = table.records[index].firstTypeName;
                if (entry == read.entries.end())
                {
                    if (label.empty() || fileHas(label))
                    {
                        problem = std::format("its sub-asset '{}' did not read, as logged above", label);
                        break;
                    }
                }
                else if (!registry.IsA(entry->type->name, was))
                {
                    problem = std::format("{} is a `{}` now, which is not the `{}` its handles read",
                                          label.empty() ? std::string("its top level")
                                                        : std::format("its sub-asset '{}'", label),
                                          entry->type->name, was);
                    break;
                }
            }
        }
        if (!problem.empty())
        {
            keep(problem);
            m_state->ReleaseOwner(Detail::ReloadOwnerId);
            return;
        }

        // Every owner of what the file loaded owns what it holds now, the targets of the references it adds included.
        std::vector<Detail::OwnerId> owners;
        for (const auto& [label, index] : held)
        {
            for (const Detail::OwnerId owner : table.records[index].owners)
            {
                if (owner != Detail::ReloadOwnerId && std::ranges::find(owners, owner) == owners.end())
                {
                    owners.push_back(owner);
                }
            }
        }
        std::vector<std::uint32_t> stored;
        for (Detail::EnvelopeEntry& entry : read.entries)
        {
            const auto found = table.byKey.find(entry.label.empty() ? fileKey : prefix + entry.label);
            assert(found != table.byKey.end() && "a reload's entry is held already or was reserved by its read");
            Detail::AssetRecord& record = table.records[found->second];
            // A sub-asset the file did not have before was reserved by the read, with no data: added, not replaced.
            const bool replacing = record.data != nullptr;
            record.data = std::move(entry.value);
            record.typeName = entry.type->name;
            record.loadDiagnostics = read.diagnostics;
            record.loadIncomplete = read.incomplete;
            record.references = std::move(entry.references);
            stored.push_back(found->second);
            if (replacing)
            {
                ++record.revision;
                const Detail::AssetSlot slot{found->second, record.generation};
                m_state->reloaded.push_back(
                    {Detail::PathOf(*m_state, Detail::AssetKind::Data, slot).value_or(record.key),
                     Detail::AssetKind::Data, slot});
            }
        }
        for (const auto& [label, index] : held)
        {
            if (entryOf(label) == read.entries.end())
            {
                m_state->logger->Warn("The {} file '{}' no longer has its sub-asset '{}': what it loaded is kept",
                                      KindName(Detail::AssetKind::Data), assetPath, label);
            }
        }
        for (const Detail::OwnerId owner : owners)
        {
            for (const std::uint32_t index : stored)
            {
                ShareData(index, owner);
            }
        }
        // Kept so a later save keeps the new file's comments (ADR-0077 §1).
        m_state->fileTrees.insert_or_assign(fileKey, std::move(*read.tree));
        m_state->ReleaseOwner(Detail::ReloadOwnerId);
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

    Detail::AssetRecord* AssetStore::EditRecord(Detail::AssetKind kind, Detail::AssetSlot slot, std::string_view what,
                                                std::string_view stale)
    {
        Detail::AssetRecord* record = m_state->Find(kind, slot);
        if (record == nullptr || record->data == nullptr)
        {
            m_state->logger->Error(stale, what, KindName(kind));
            return nullptr;
        }
        return record;
    }

    void* AssetStore::EditData(Detail::AssetSlot slot, const TypeDescription& type, std::string_view action)
    {
        Detail::AssetRecord* record = EditRecord(Detail::AssetKind::Data, slot, action);
        if (record == nullptr)
        {
            return nullptr;
        }
        if (!m_state->types->IsA(record->typeName, type.name))
        {
            m_state->logger->Error("Could not {}: the data asset handle names a `{}`, which is not a `{}`", action,
                                   record->typeName, type.name);
            return nullptr;
        }
        ++record->revision;
        // A data asset type extends at most one other and has no virtual functions (ADR-0075), so its base is at the
        // instance's own address.
        return static_cast<Detail::DataValue*>(record->data.get())->value;
    }
} // namespace Fenrir
