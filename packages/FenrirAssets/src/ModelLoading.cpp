// Turning an imported model file into assets (ADR-0021, ADR-0024, ADR-0031): its images, materials, skeletons, meshes,
// clips and the model itself, each stored under the file's path and a label.
#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"
#include "AssetWatcher.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "Importers/ImageImporter.hpp"
#include "Importers/ModelImporter.hpp"

namespace Fenrir
{
    namespace
    {
        // A 1x1 opaque white image, standing in for a texture that could not be loaded.
        ImageData WhitePixel()
        {
            ImageData image;
            image.width = 1;
            image.height = 1;
            image.pixels.assign(4, std::byte{0xff});
            return image;
        }

        // Base colour and emissive are colours, encoded sRGB; every other texture is data, read linear (ADR-0024).
        ColourSpace ColourSpaceOf(std::string_view parameter)
        {
            return parameter == "baseColourTexture" || parameter == "emissiveTexture" ? ColourSpace::Srgb
                                                                                      : ColourSpace::Linear;
        }

        std::string_view ColourSpaceName(ColourSpace space)
        {
            return space == ColourSpace::Srgb ? "sRGB" : "linear";
        }

        std::string_view BuiltinName(Detail::ImportedShader shader)
        {
            return shader == Detail::ImportedShader::Unlit ? "builtin:unlit" : "builtin:lit";
        }

        // The kinds of asset a model file is stored as: the model, then the sub-assets it holds by label.
        constexpr std::array ModelFileKinds{Detail::AssetKind::Model,    Detail::AssetKind::Mesh,
                                            Detail::AssetKind::Material, Detail::AssetKind::Image,
                                            Detail::AssetKind::Skeleton, Detail::AssetKind::Clip};

        template <typename T>
        AssetHandle<T> HandleOf(Detail::AssetSlot slot)
        {
            return {slot.index, slot.generation};
        }
    } // namespace

    std::string AssetStore::ImportModelFile(const std::filesystem::path& file, const std::string& fileKey,
                                            std::uint32_t owner)
    {
        const bool reloading = owner == Detail::ReloadOwnerId;
        // What the store holds from the file, which a reload replaces, and every owner of any of it, who own what the
        // reloaded file adds.
        std::vector<std::pair<Detail::AssetKind, std::uint32_t>> held;
        std::vector<Detail::OwnerId> owners;
        if (reloading)
        {
            const std::string prefix = fileKey + '#';
            for (const Detail::AssetKind kind : ModelFileKinds)
            {
                const Detail::AssetTable& table = m_state->tables[static_cast<std::size_t>(kind)];
                for (const auto& [key, index] : table.byKey)
                {
                    if (key != fileKey && !key.starts_with(prefix))
                    {
                        continue;
                    }
                    held.emplace_back(kind, index);
                    for (const Detail::OwnerId holder : table.records[index].owners)
                    {
                        if (std::ranges::find(owners, holder) == owners.end())
                        {
                            owners.push_back(holder);
                        }
                    }
                }
            }
            if (held.empty())
            {
                return {};
            }
        }

        // Looked at before the import, so a write during it is a change the watcher sees.
        const std::optional<ReloadWatcher::Stamp> stamp = m_state->watcher ? Detail::StampOf(file) : std::nullopt;
        // Imported whole before anything is stored, so a file that does not import leaves the store as it was.
        Detail::ModelImport imported = Detail::ImportModel(file);
        if (!imported.error.empty())
        {
            return std::move(imported.error);
        }
        ILogger& logger = *m_state->logger;
        for (const std::string& warning : imported.warnings)
        {
            logger.Warn("Model '{}': {}", fileKey, warning);
        }

        // A reload is all or nothing (ADR-0081 §2): each asset the store holds from the file waits here with its new
        // data, and each the file adds is noted, until the whole file is known to have imported.
        struct Replacement
        {
            Detail::AssetKind kind;
            std::uint32_t index;
            Detail::AssetBox data;
        };
        std::vector<Replacement> replacements;
        std::vector<std::pair<Detail::AssetKind, std::uint32_t>> added;
        const auto replace = [&](Detail::AssetKind kind, std::uint32_t index, Detail::AssetBox data) {
            replacements.push_back({kind, index, std::move(data)});
            return Detail::AssetSlot{index, m_state->tables[static_cast<std::size_t>(kind)].records[index].generation};
        };
        // Each sub-asset is stored under the file's key and its label. One already stored under it (the model was
        // unloaded, but its sub-assets live on) is shared instead of stored again, or replaced by a reload; one a
        // reloaded file adds is stored for every owner of what the file loaded.
        const auto store = [&](Detail::AssetKind kind, Detail::AssetBox data, std::string_view label) {
            Detail::AssetTable& table = m_state->tables[static_cast<std::size_t>(kind)];
            std::string key = fileKey + '#' + std::string(label);
            if (const auto found = table.byKey.find(key); found != table.byKey.end())
            {
                if (reloading)
                {
                    return replace(kind, found->second, std::move(data));
                }
                Detail::AddOwner(table.records[found->second], owner);
                return Detail::AssetSlot{found->second, table.records[found->second].generation};
            }
            if (!reloading)
            {
                return Detail::InsertAsset(table, std::move(data), owner, std::move(key));
            }
            const Detail::AssetSlot slot = Detail::InsertAsset(table, std::move(data), owners.front(), std::move(key));
            for (const Detail::OwnerId holder : owners)
            {
                Detail::AddOwner(table.records[slot.index], holder);
            }
            added.emplace_back(kind, slot.index);
            return slot;
        };

        // Images: decoded, or white in their place, then given the colour space of their first use.
        std::vector<ImageData> images;
        images.reserve(imported.images.size());
        for (Detail::ImportedImage& source : imported.images)
        {
            std::string problem = std::move(source.error);
            if (problem.empty() && !source.file.empty() && !Detail::ReadAssetFile(source.file, source.bytes))
            {
                problem = "its file could not be read";
            }
            if (problem.empty())
            {
                Detail::ImageImport decoded = Detail::ImportImage(source.bytes);
                Detail::AssetWarnings imageWarnings;
                if (decoded.image && Detail::PrepareAsset(*decoded.image, imageWarnings).empty())
                {
                    images.push_back(std::move(*decoded.image));
                    continue;
                }
                problem = decoded.image ? "its pixels do not fit its size" : std::move(decoded.error);
            }
            // A reload keeps the image it has rather than whitening it: the texture is a unit the file left out.
            if (reloading)
            {
                imported.failures.push_back(
                    std::format("its texture '{}' could not be loaded ({})", source.name, problem));
            }
            else
            {
                logger.Warn("Model '{}': its texture '{}' could not be loaded ({}); a 1x1 white image stands in",
                            fileKey, source.name, problem);
            }
            images.push_back(WhitePixel());
        }
        std::vector<std::optional<ColourSpace>> firstUse(images.size());
        for (const Detail::ImportedMaterial& material : imported.materials)
        {
            for (const Detail::ImportedTexture& texture : material.textures)
            {
                const ColourSpace use = ColourSpaceOf(texture.parameter);
                std::optional<ColourSpace>& first = firstUse[texture.image];
                if (!first)
                {
                    first = use;
                    images[texture.image].colourSpace = use;
                }
                else if (*first != use)
                {
                    logger.Warn("Model '{}': image '{}' is read as {} by material '{}' ({}), but was first used as {}; "
                                "it stays {}",
                                fileKey, imported.images[texture.image].name, ColourSpaceName(use), material.name,
                                texture.parameter, ColourSpaceName(*first), ColourSpaceName(*first));
                }
            }
        }

        Model model;
        for (std::size_t i = 0; i < images.size(); ++i)
        {
            Detail::AssetBox data(new ImageData(std::move(images[i])), &Detail::DestroyAssetData<ImageData>);
            model.images.push_back(
                HandleOf<Image>(store(Detail::AssetKind::Image, std::move(data), std::format("Image{}", i))));
        }

        // Materials: overrides on the built-in shader they map to, named before the renderer registers it.
        for (std::size_t i = 0; i < imported.materials.size(); ++i)
        {
            Detail::ImportedMaterial& source = imported.materials[i];
            auto* material = new MaterialData;
            Detail::AssetBox data(material, &Detail::DestroyAssetData<MaterialData>);
            material->shader = HandleOf<Shader>(ReserveBuiltin(Detail::AssetKind::Shader, BuiltinName(source.shader)));
            material->blendMode = source.blendMode;
            material->alphaCutoff = source.alphaCutoff;
            material->doubleSided = source.doubleSided;
            for (auto& [name, value] : source.factors)
            {
                material->overrides.insert_or_assign(std::move(name), std::move(value));
            }
            for (const Detail::ImportedTexture& texture : source.textures)
            {
                material->overrides.insert_or_assign(texture.parameter,
                                                     MaterialTexture{model.images[texture.image], texture.sampler});
            }
            model.materials.push_back(
                HandleOf<Material>(store(Detail::AssetKind::Material, std::move(data), std::format("Material{}", i))));
        }

        // Skeletons: already checked and derived by the importer (ResolveSkinning), each with its owner.
        for (std::size_t i = 0; i < imported.skeletons.size(); ++i)
        {
            Detail::ImportedSkeleton& source = imported.skeletons[i];
            Detail::AssetBox data(new SkeletonData(std::move(source.data)), &Detail::DestroyAssetData<SkeletonData>);
            model.skeletons.push_back(
                HandleOf<Skeleton>(store(Detail::AssetKind::Skeleton, std::move(data), std::format("Skeleton{}", i))));
            model.skeletonOwners.push_back(static_cast<std::int32_t>(source.owner));
        }

        // Meshes: each checked as Add checks one; a mesh that fails is left out, and one whose skin or skeleton
        // failed is never imported unskinned.
        std::vector<std::optional<ModelMesh>> meshes(imported.meshes.size());
        for (std::size_t i = 0; i < imported.meshes.size(); ++i)
        {
            Detail::ImportedMesh& source = imported.meshes[i];
            if (source.failed)
            {
                continue;
            }
            Detail::AssetWarnings meshWarnings;
            if (std::string problem = Detail::PrepareAsset(source.data, meshWarnings); !problem.empty())
            {
                // A reload that left a mesh out would give the next mesh its label, so it counts as failed too.
                if (source.skeleton >= 0 || reloading)
                {
                    imported.failures.push_back(std::format("mesh '{}' is left out: {}", source.name, problem));
                }
                else
                {
                    logger.Warn("Model '{}': mesh '{}' is left out: {}", fileKey, source.name, problem);
                }
                continue;
            }
            for (const std::string& warning : meshWarnings)
            {
                logger.Warn("Model '{}': mesh '{}': {}", fileKey, source.name, warning);
            }
            Detail::AssetBox data(new MeshData(std::move(source.data)), &Detail::DestroyAssetData<MeshData>);
            const auto mesh = HandleOf<Mesh>(
                store(Detail::AssetKind::Mesh, std::move(data), std::format("Mesh{}", model.meshes.size())));
            model.meshes.push_back(mesh);
            meshes[i] = ModelMesh{mesh, model.materials[source.material], source.skeleton};
        }

        // Clips: checked as Add checks one; a clip that fails is left out.
        for (Detail::ImportedClip& source : imported.clips)
        {
            Detail::AssetWarnings clipWarnings;
            if (std::string problem = Detail::PrepareAsset(source.data, clipWarnings); !problem.empty())
            {
                imported.failures.push_back(std::format("clip '{}' is left out: {}", source.name, problem));
                continue;
            }
            for (const std::string& warning : clipWarnings)
            {
                logger.Warn("Model '{}': clip '{}': {}", fileKey, source.name, warning);
            }
            Detail::AssetBox data(new ClipData(std::move(source.data)), &Detail::DestroyAssetData<ClipData>);
            model.clips.push_back(HandleOf<Clip>(
                store(Detail::AssetKind::Clip, std::move(data), std::format("Clip{}", model.clips.size()))));
        }

        for (Detail::ImportedNode& source : imported.nodes)
        {
            ModelNode node;
            node.name = std::move(source.name);
            node.sourceId = source.sourceId;
            node.rest = source.rest;
            node.parent = source.parent;
            node.kind = source.kind;
            node.skeleton = source.skeleton;
            node.bone = source.bone;
            node.light = source.light;
            for (const std::uint32_t mesh : source.meshes)
            {
                if (meshes[mesh])
                {
                    node.meshes.push_back(*meshes[mesh]);
                }
            }
            model.nodes.push_back(std::move(node));
        }

        // A reloaded file that left any unit out replaces nothing: what it added goes, and the store is as it was.
        if (reloading && !imported.failures.empty())
        {
            for (const auto& [kind, index] : added)
            {
                m_state->Free(kind, index);
            }
            std::string problem = "it imported only in part";
            for (const std::string& failure : imported.failures)
            {
                problem += "; " + failure;
            }
            return problem;
        }
        // What failed leaves the rest usable: the load is Partial, and each load of the file says so.
        for (const std::string& failure : imported.failures)
        {
            logger.Error("Model '{}': {}", fileKey, failure);
        }
        if (imported.failures.empty())
        {
            m_state->modelFailures.erase(fileKey);
        }
        else
        {
            m_state->modelFailures.insert_or_assign(fileKey, std::move(imported.failures));
        }

        Detail::AssetBox data(new Model(std::move(model)), &Detail::DestroyAssetData<Model>);
        Detail::AssetTable& models = m_state->tables[static_cast<std::size_t>(Detail::AssetKind::Model)];
        if (!reloading)
        {
            Detail::InsertAsset(models, std::move(data), owner, fileKey);
        }
        // A model unloaded while its sub-assets live on is not stored again by their reload: the next load of it
        // imports the file afresh.
        else if (const auto found = models.byKey.find(fileKey); found != models.byKey.end())
        {
            replace(Detail::AssetKind::Model, found->second, std::move(data));
        }
        // The file imported whole: each held asset takes its new data, a revision on, listed in Reloaded().
        for (Replacement& replacement : replacements)
        {
            const Detail::AssetRecord& record =
                m_state->tables[static_cast<std::size_t>(replacement.kind)].records[replacement.index];
            const Detail::AssetSlot slot{replacement.index, record.generation};
            if (ReplaceSlot(replacement.kind, slot, std::move(replacement.data)))
            {
                m_state->reloaded.push_back(
                    {Detail::PathOf(*m_state, replacement.kind, slot).value_or(record.key), replacement.kind, slot});
            }
            std::erase(held, std::pair(replacement.kind, replacement.index));
        }

        // A sub-asset the reloaded file no longer has keeps what it loaded, as a data asset's does (ADR-0081 §2).
        const std::string assetPath = Detail::RootRelative(m_state->root, file).value_or(fileKey);
        for (const auto& [kind, index] : held)
        {
            const std::string& key = m_state->tables[static_cast<std::size_t>(kind)].records[index].key;
            logger.Warn("The model file '{}' no longer has its sub-asset '{}': what it loaded is kept", assetPath,
                        key.substr(key.find('#') + 1));
        }
        if (m_state->watcher)
        {
            m_state->watcher->Track(fileKey, assetPath, Detail::AssetKind::Model, stamp, ReloadWatcher::Clock::now());
        }
        return {};
    }
} // namespace Fenrir
