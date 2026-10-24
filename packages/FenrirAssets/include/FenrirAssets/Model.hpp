#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/DefaultComponents.hpp"

namespace Fenrir
{
    /**
     * @brief What a model node is. In the foundation every node is ordinary; the animation spec adds synthetic owners
     * and bones (a bone's index in its skeleton, and the skeleton's, ADR-0031).
     */
    enum class ModelNodeKind : std::uint8_t
    {
        /** @brief A node spawned as an entity. */
        Ordinary,
    };

    /** @brief One mesh a model node draws, with the material it draws with. */
    struct ModelMesh
    {
        /** @brief The mesh, a sub-asset of the model (`file#Mesh0`, `#Mesh1`...). */
        AssetHandle<Mesh> mesh;
        /** @brief The material its one submesh draws with, a sub-asset of the model (`file#Material0`...). */
        AssetHandle<Material> material;
    };

    /** @brief One node of a model file, as the file has it. */
    struct ModelNode
    {
        /** @brief The node's name in the file; may be empty, and need not be unique. */
        std::string name;
        /**
         * @brief The node's identity in its file, unique among the file's nodes and stable for one import of one file:
         * a glTF node index, or ufbx's id for an FBX or OBJ node. Not a name, so it is exact when names repeat.
         */
        std::uint64_t sourceId = 0;
        /** @brief The node's rest transform relative to its parent: position, unit rotation and scale. */
        Transform rest;
        /** @brief The parent's index in Model::nodes, always lower than this node's own; -1 for a root node. */
        std::int32_t parent = -1;
        /** @brief What the node is. */
        ModelNodeKind kind = ModelNodeKind::Ordinary;
        /** @brief The meshes the node draws, each with its material. */
        std::vector<ModelMesh> meshes;
    };

    /**
     * @brief A model file as plain data (ADR-0021, ADR-0031): its whole node tree, parents first, pointing at its
     * meshes and materials, and the images those use. Each mesh, material and image is a sub-asset of the model, an
     * asset of its own that also loads alone by label: `scene.gltf#Mesh0`, `#Material0`, `#Image0`, numbered as in
     * these lists. A model owns nothing: unloading it leaves its sub-assets, and each has its own owners.
     *
     * Every mesh holds one submesh. A source mesh with several materials (glTF primitives, FBX material parts) is one
     * mesh per material, in the file's order.
     */
    struct Model
    {
        /** @brief Every node of the file, each before its children, in the file's order otherwise. */
        std::vector<ModelNode> nodes;
        /** @brief The model's meshes, by label number. A mesh skinned in its file is left out (static content only). */
        std::vector<AssetHandle<Mesh>> meshes;
        /** @brief The model's materials, by label number. */
        std::vector<AssetHandle<Material>> materials;
        /** @brief The images its materials use, by label number. One that failed to load is a 1x1 white image. */
        std::vector<AssetHandle<Image>> images;
    };
} // namespace Fenrir
