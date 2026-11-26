#include "FenrirAssets/AssetStore.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
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
#include "Envelope.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "Importers/ImageImporter.hpp"
#include "Importers/ModelImporter.hpp"

namespace Fenrir
{
    namespace
    {
        using Detail::BuiltinPrefix;
        using Detail::Utf8Path;
        using Detail::Utf8Text;

        // The words log messages use for each kind, in AssetKind's order.
        constexpr std::array<std::string_view, Detail::AssetKindCount> kindNames = {
            "mesh", "image",     "material",      "shader", "model",     "skeleton",
            "clip", "bone mask", "instance list", "font",   "data asset"};

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

        // `file`, absolute and normal, relative to `root` with forward slashes: its asset path (ADR-0076). None when it
        // is not under the root.
        std::optional<std::string> RootRelative(const std::filesystem::path& root, const std::filesystem::path& file)
        {
            const std::filesystem::path relative = file.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() || relative.has_root_name() || *relative.begin() == "..")
            {
                return std::nullopt;
            }
            return Utf8Text(relative);
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
                record.key.clear();
            }
            record.typeName = {};
            record.loadDiagnostics.clear();
            record.loadIncomplete = false;
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
            // In AssetKind's order, as a description names each kind's handle target.
            constexpr std::array<std::string_view, AssetKindCount - 1> targets = {
                "Fenrir::Mesh",     "Fenrir::Image", "Fenrir::Material", "Fenrir::Shader",       "Fenrir::Model",
                "Fenrir::Skeleton", "Fenrir::Clip",  "Fenrir::BoneMask", "Fenrir::InstanceList", "Fenrir::Font"};
            const auto found = std::ranges::find(targets, target);
            if (found == targets.end())
            {
                return std::nullopt;
            }
            return static_cast<AssetKind>(std::distance(targets.begin(), found));
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
            const Detail::OwnerId ownerId = Detail::OwnerIdOf(m_state, owner);
            Detail::AddOwner(record, ownerId);
            if (kind == Detail::AssetKind::Model)
            {
                ShareModel(*static_cast<const Model*>(record.data.get()), ownerId);
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
            Detail::AddOwner(record, Detail::OwnerIdOf(m_state, owner));
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
            FontData font;
            if (!Detail::ReadAssetFile(resolved.file, font.bytes))
            {
                return fail(LoadFailure::ImportFailed, "the file could not be read");
            }
            Detail::AssetWarnings warnings;
            if (std::string problem = Detail::PrepareAsset(font, warnings); !problem.empty())
            {
                return fail(LoadFailure::ImportFailed, std::move(problem));
            }
            Detail::AssetBox data(new FontData(std::move(font)), &Detail::DestroyAssetData<FontData>);
            return Detail::InsertAsset(table, std::move(data), Detail::OwnerIdOf(m_state, owner), resolved.key);
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

    Detail::AssetSlot AssetStore::LoadDataSlot(std::string_view path, AssetOwner owner, const TypeDescription& type,
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
            return fail(LoadFailure::NotADataAsset,
                        std::format("the type registry has no `{}`: register its package's types first", type.name));
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
            const Detail::OwnerId ownerId = Detail::OwnerIdOf(m_state, owner);
            Detail::AddOwner(record, ownerId);
            // Loading a file's top level loads its sub-assets for the same owner, as a model's are (ADR-0076).
            if (!resolved.labelled)
            {
                const std::string prefix = resolved.key + '#';
                for (const auto& [key, index] : table.byKey)
                {
                    if (key.starts_with(prefix))
                    {
                        Detail::AddOwner(table.records[index], ownerId);
                    }
                }
            }
            diagnostics = record.loadDiagnostics;
            incomplete = record.loadIncomplete;
            return {loaded->second, record.generation};
        }

        Detail::AssetSource& source = *m_state->source;
        const std::optional<std::vector<std::byte>> bytes = source.Read(*assetPath);
        if (!bytes)
        {
            return source.Exists(*assetPath) ? fail(LoadFailure::ImportFailed, "the file could not be read")
                                             : fail(LoadFailure::FileNotFound, "there is no file at this path");
        }

        // What a reference in this file names (ADR-0076 §2, §3): its target, loaded first with this load's owner, of
        // the kind the field holds and, for a data asset, of a type extending the field's; or why it names none.
        const auto readReference = [&](std::string_view reference, const FieldInfo& field) -> Detail::ReferenceRead {
            // A bare `#name` is a sub-asset of the file that names it (ADR-0076).
            const std::string target =
                reference.starts_with('#') ? *assetPath + std::string(reference) : std::string(reference);
            const std::string_view targetFile = std::string_view(target).substr(0, target.rfind('#'));
            const auto refuse = [](std::string problem) { return Detail::ReferenceRead{{}, std::move(problem)}; };
            // The target's load logs nothing of its own: the problem is reported once, at the path in this file.
            std::optional<LoadDiagnostic> failure;
            Detail::AssetSlot slot;
            std::string expected;

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
                return {{slot.index, slot.generation}, {}};
            }
            if (failure->reason == LoadFailure::FileNotFound)
            {
                return refuse(std::format("expected {}, found no file", expected));
            }
            return refuse(std::format("expected {}, found a file that did not load: {} ({})", expected,
                                      ReasonText(failure->reason), failure->importerMessage));
        };

        const std::string fileKey = Utf8Text(resolved.file);
        const Detail::OwnerId ownerId = Detail::OwnerIdOf(m_state, owner);
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
            const Detail::AssetSlot slot =
                Detail::InsertAsset(table, Detail::AssetBox(nullptr, &Detail::DestroyAssetData<Detail::DataValue>),
                                    ownerId, std::move(key));
            m_state->Find(Detail::AssetKind::Data, slot)->typeName = entryType.name;
        };
        hooks.readReference = readReference;
        Detail::EnvelopeRead read = Detail::ReadDataEnvelope(
            *bytes, fileKey, resolved.labelled ? std::optional<std::string_view>(resolved.label) : std::nullopt, type,
            registry, hooks, *m_state->logger);
        diagnostics = std::move(read.diagnostics);
        if (read.failure)
        {
            return fail(*read.failure, std::move(read.failureMessage));
        }
        incomplete = read.incomplete;

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
                Detail::AddOwner(record, ownerId);
                slot = {found->second, record.generation};
                if (record.data == nullptr)
                {
                    record.data = std::move(stored.value);
                    record.loadDiagnostics = diagnostics;
                    record.loadIncomplete = incomplete;
                }
            }
            else
            {
                slot = Detail::InsertAsset(table, std::move(stored.value), ownerId, std::move(key));
                Detail::AssetRecord& record = *m_state->Find(Detail::AssetKind::Data, slot);
                record.typeName = stored.type->name;
                record.loadDiagnostics = diagnostics;
                record.loadIncomplete = incomplete;
            }
            if (entry == read.requested)
            {
                requested = slot;
            }
        }
        return requested;
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
