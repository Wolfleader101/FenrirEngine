#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/SkeletonData.hpp"
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

    /**
     * @brief One imported mesh: one submesh's worth of a source mesh, drawn with one material. A skinned one is read
     * as its file has it, then ResolveSkinning bakes it into its skeleton's bind space.
     */
    struct ImportedMesh
    {
        std::string name;
        /** @brief Its streams and indices, with one submesh over every index; a skin section once resolved. */
        MeshData data;
        /** @brief Its material, by index into ModelImport::materials. */
        std::uint32_t material = 0;
        /** @brief The skin it is drawn with, by index into ModelImport::skins; -1 for a rigid mesh. */
        std::int32_t skin = -1;
        /** @brief As read, per vertex: four indices into its skin's joints. */
        std::vector<std::array<std::uint32_t, 4>> joints;
        /** @brief As read, per vertex: the weights of those four joints. */
        std::vector<Math::Vec4> weights;
        /** @brief Once resolved, the skeleton it was baked against, by index into ModelImport::skeletons; else -1. */
        std::int32_t skeleton = -1;
        /** @brief Whether its skin or skeleton failed, so that it is left out: never imported unskinned. */
        bool failed = false;
    };

    /** @brief One skin of the file: the joints its meshes' vertices name, and their inverse binds. */
    struct ImportedSkin
    {
        std::string name;
        /** @brief Its joints, by index into ModelImport::nodes, in the file's order. */
        std::vector<std::uint32_t> joints;
        /**
         * @brief One inverse bind per joint, in the space the backend reads nodes' rest worlds in (glTF's scene-root
         * space, ufbx's world); identity matrices when the file gives none.
         */
        std::vector<Math::Mat4> inverseBinds;
        /** @brief The node the file names as the skin's skeleton root (glTF's `skin.skeleton`), or -1. */
        std::int32_t skeletonNode = -1;
    };

    /** @brief One animation of the file, its channels' targets named by node. */
    struct ImportedClip
    {
        std::string name;
        /** @brief Its channels, their targets left for ResolveSkinning to fill in. */
        ClipData data;
        /** @brief Each channel's target, by index into ModelImport::nodes. */
        std::vector<std::uint32_t> targets;
    };

    /** @brief One skeleton ResolveSkinning made: its bones, checked as the store checks one, and its owner. */
    struct ImportedSkeleton
    {
        /** @brief Its bones, prepared, so that its engine names and bind signature are set. */
        SkeletonData data;
        /** @brief Its owner, the node of its skeleton entity, by index into ModelImport::nodes. */
        std::uint32_t owner = 0;
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
        /** @brief Its point or spot light, if it has one. */
        std::optional<ModelLight> light;
        /** @brief Whether the file gives it a mesh, camera or light, so that an animation cannot make it a bone. */
        bool carriesContent = false;
        /** @brief Whether the format marks it as a bone: an FBX skeleton node (`LimbNode`, `Limb` or `Root`). */
        bool markedBone = false;
        /** @brief What it is, once ResolveSkinning has run; for a bone, `skeleton` and `bone` say whose. */
        ModelNodeKind kind = ModelNodeKind::Ordinary;
        std::int32_t skeleton = -1;
        std::int32_t bone = -1;
    };

    /**
     * @brief What a model importer made of a file: plain data, with sub-assets named by index, that the store turns
     * into assets. Nodes come parents first. A backend fills the nodes, meshes, skins and clips as its file has them;
     * ResolveSkinning then makes the skeletons and puts every skinned mesh in one bind space (ADR-0031).
     */
    struct ModelImport
    {
        std::vector<ImportedNode> nodes;
        std::vector<ImportedMesh> meshes;
        std::vector<ImportedMaterial> materials;
        std::vector<ImportedImage> images;
        std::vector<ImportedSkin> skins;
        std::vector<ImportedClip> clips;
        /** @brief Filled by ResolveSkinning. */
        std::vector<ImportedSkeleton> skeletons;
        /** @brief Problems that leave the model usable (a primitive skipped, a repeated bone name). */
        std::vector<std::string> warnings;
        /**
         * @brief Each unit that failed and was left out (a skeleton with its skins and meshes, a skin with its meshes,
         * a clip), saying why; the load is then `Partial`.
         */
        std::vector<std::string> failures;
        /** @brief Why the file could not be imported at all; empty when it was. */
        std::string error;
    };

    /** @brief Whether `file` is a model file an importer reads, by its extension: glTF, GLB, FBX or OBJ. */
    [[nodiscard]] bool IsModelFile(const std::filesystem::path& file);

    /**
     * @brief Reads the model file at `file` (absolute) with the importer for its format, then normalises what it gave:
     * rotations made unit quaternions, normals generated for meshes without them, and skins resolved into skeletons
     * (ResolveSkinning). The private model importer seam (ADR-0021, ADR-0031): cgltf reads glTF and GLB, ufbx reads
     * FBX and OBJ, and nothing outside FenrirAssets's sources sees either.
     */
    [[nodiscard]] ModelImport ImportModel(const std::filesystem::path& file);

    /** @brief cgltf's half of the seam: a glTF or GLB file, before normalisation. */
    [[nodiscard]] ModelImport ImportGltf(const std::filesystem::path& file);

    /** @brief ufbx's half of the seam: an FBX or OBJ file, before normalisation. */
    [[nodiscard]] ModelImport ImportUfbx(const std::filesystem::path& file);

    /**
     * @brief The format-independent rules of skinned import (ADR-0031), run on what a backend read: which nodes are
     * bones; skins grouped into skeletons by joint identity (or by one tree of format-marked bones), merged when their
     * binds agree or one conversion takes one into the other; marked bones no skin reaches made skeletons of their own,
     * bound at their rest; each skeleton's owner, with synthetic owners added where several share a node; every skinned
     * mesh baked into its skeleton's bind space, its joints remapped and its weights quantised; clip rotations kept in
     * one hemisphere, and each channel's target named. Each error fails its smallest unit into `failures`, its meshes
     * marked failed. An ordinary node with a degenerate transform is a warning.
     */
    void ResolveSkinning(ModelImport& model);

    /**
     * @brief Moves a mesh's vertices by `matrix`: positions by it, normals by its inverse transpose (renormalised),
     * tangents by its upper 3x3 (renormalised), with their handedness flipped when its determinant is negative.
     */
    void BakeVertices(MeshData& mesh, const Math::Mat4& matrix);
} // namespace Fenrir::Detail
