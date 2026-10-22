#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirECS/DefaultComponents.hpp"

namespace Fenrir::Detail
{
    /** @brief The built-in shader an imported material draws with. */
    enum class ImportedShader : std::uint8_t
    {
        /** @brief `builtin:lit`. */
        Lit,
        /** @brief `builtin:unlit`, for a material the file marks unlit. */
        Unlit,
    };

    /** @brief One texture parameter of an imported material: the image, by index into ModelImport::images. */
    struct ImportedTexture
    {
        /** @brief The standard parameter name: `baseColourTexture`, `normalTexture`... */
        std::string parameter;
        std::uint32_t image = 0;
        SamplerDescription sampler;
    };

    /** @brief A file's material, mapped onto the built-ins' standard parameter names. */
    struct ImportedMaterial
    {
        std::string name;
        ImportedShader shader = ImportedShader::Lit;
        BlendMode blendMode = BlendMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        /** @brief Every factor the file gives, or the format's own default, by standard parameter name. */
        std::vector<std::pair<std::string, MaterialValue>> factors;
        std::vector<ImportedTexture> textures;
    };

    /**
     * @brief Where an image's encoded bytes are: a file beside the model, or bytes inside it. The store decodes both;
     * a backend that could not find them says why in `error`.
     */
    struct ImportedImage
    {
        /** @brief How log messages name it: its file name, or where in the model it is. */
        std::string name;
        /** @brief The image file, absolute; empty for an image inside the model. */
        std::filesystem::path file;
        /** @brief The image's encoded bytes, for an image inside the model. */
        std::vector<std::byte> bytes;
        /** @brief Why the backend has no bytes or file for it; empty when it has. */
        std::string error;
    };

    /** @brief One imported mesh: one submesh's worth of a source mesh, drawn with one material. */
    struct ImportedMesh
    {
        std::string name;
        /** @brief Its streams and indices, with one submesh over every index. */
        MeshData data;
        /** @brief Its material, by index into ModelImport::materials. */
        std::uint32_t material = 0;
    };

    /** @brief One source node. */
    struct ImportedNode
    {
        std::string name;
        std::uint64_t sourceId = 0;
        Transform rest;
        /** @brief Its parent's index in ModelImport::nodes, lower than its own; -1 for a root. */
        std::int32_t parent = -1;
        /** @brief Its meshes, by index into ModelImport::meshes. */
        std::vector<std::uint32_t> meshes;
    };

    /**
     * @brief What a model importer made of a file: plain data, with sub-assets named by index, that the store turns
     * into assets. Nodes come parents first; skinned meshes are already left out, each with a warning naming it.
     */
    struct ModelImport
    {
        std::vector<ImportedNode> nodes;
        std::vector<ImportedMesh> meshes;
        std::vector<ImportedMaterial> materials;
        std::vector<ImportedImage> images;
        /** @brief Problems that leave the model usable (a skinned mesh left out, a primitive skipped). */
        std::vector<std::string> warnings;
        /** @brief Why the file could not be imported at all; empty when it was. */
        std::string error;
    };

    /** @brief Whether `file` is a model file an importer reads, by its extension: glTF, GLB, FBX or OBJ. */
    [[nodiscard]] bool IsModelFile(const std::filesystem::path& file);

    /**
     * @brief Reads the model file at `file` (absolute) with the importer for its format, then normalises what it gave:
     * rotations made unit quaternions, and normals generated for meshes without them. The private model importer seam
     * (ADR-0021, ADR-0031): cgltf reads glTF and GLB, ufbx reads FBX and OBJ, and nothing outside FenrirAssets's
     * sources sees either.
     */
    [[nodiscard]] ModelImport ImportModel(const std::filesystem::path& file);

    /** @brief cgltf's half of the seam: a glTF or GLB file, before normalisation. */
    [[nodiscard]] ModelImport ImportGltf(const std::filesystem::path& file);

    /** @brief ufbx's half of the seam: an FBX or OBJ file, before normalisation. */
    [[nodiscard]] ModelImport ImportUfbx(const std::filesystem::path& file);
} // namespace Fenrir::Detail
