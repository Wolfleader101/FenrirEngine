#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/DefaultComponents.hpp"

namespace Fenrir
{
    /** @brief What a model node is (ADR-0031). */
    enum class ModelNodeKind : std::uint8_t
    {
        /** @brief A node spawned as an entity: an ordinary node. */
        Ordinary,
        /**
         * @brief A node the importer added so that a skeleton has a skeleton entity of its own when several would share
         * one node: an identity child of that node, named `<first root bone>.Skeleton`, spawned like an ordinary node.
         */
        SyntheticOwner,
        /** @brief A bone of one of the model's skeletons (`ModelNode::skeleton`, `ModelNode::bone`): not spawned. */
        Bone,
    };

    /** @brief One mesh a model node draws, with the material it draws with. */
    struct ModelMesh
    {
        /** @brief The mesh, a sub-asset of the model (`file#Mesh0`, `#Mesh1`...). */
        AssetHandle<Mesh> mesh;
        /** @brief The material its one submesh draws with, a sub-asset of the model (`file#Material0`...). */
        AssetHandle<Material> material;
        /**
         * @brief For a skinned mesh, the skeleton it was baked against, by index into Model::skeletons; -1 for a rigid
         * mesh. A skinned mesh's vertices are in that skeleton's bind space, so its node's own transform does not
         * place it: the skeleton's owner does (ADR-0031, ADR-0035).
         */
        std::int32_t skeleton = -1;
    };

    /** @brief Which kind of light a ModelLight is. */
    enum class ModelLightKind : std::uint8_t
    {
        /** @brief Shines from the node in every direction. */
        Point,
        /** @brief Shines from the node along its -Z in a cone. */
        Spot,
    };

    /**
     * @brief A light a model node carries: a glTF `KHR_lights_punctual` point or spot light, as the file gives it.
     * SpawnModel makes it the renderer's PointLight or SpotLight. A spot light shines along its node's -Z.
     */
    struct ModelLight
    {
        ModelLightKind kind = ModelLightKind::Point;
        /** @brief Linear RGB. */
        Math::Vec3 colour{1.0f};
        /** @brief The file's intensity: candela in glTF, which the renderer takes as a plain multiplier. */
        float intensity = 1.0f;
        /** @brief The distance at which the light reaches zero; 0 when the file leaves it out (no limit in glTF). */
        float range = 0.0f;
        /** @brief A spot light's cone, in radians off its axis: glTF's 0 and 45 degrees by default. */
        float innerAngle = 0.0f;
        float outerAngle = 0.785398163f;
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
        /** @brief For a bone, its skeleton's index in Model::skeletons; -1 otherwise. */
        std::int32_t skeleton = -1;
        /** @brief For a bone, its index in that skeleton; -1 otherwise. */
        std::int32_t bone = -1;
        /** @brief The meshes the node draws, each with its material. */
        std::vector<ModelMesh> meshes;
        /** @brief The point or spot light the node carries, if any (glTF's `KHR_lights_punctual`). */
        std::optional<ModelLight> light;
    };

    /**
     * @brief A model file as plain data (ADR-0021, ADR-0031): its whole node tree, parents first, pointing at its
     * meshes and materials, the images those use, and its skeletons and clips. Each mesh, material, image, skeleton and
     * clip is a sub-asset of the model, an asset of its own that also loads alone by label: `scene.gltf#Mesh0`,
     * `#Material0`, `#Image0`, `#Skeleton0`, `#Clip0`, numbered as in these lists. A model owns nothing: unloading it
     * leaves its sub-assets, and each has its own owners.
     *
     * Every mesh holds one submesh. A source mesh with several materials (glTF primitives, FBX material parts) is one
     * mesh per material, in the file's order.
     */
    struct Model
    {
        /** @brief Every node of the file, each before its children, in the file's order otherwise. */
        std::vector<ModelNode> nodes;
        /**
         * @brief The model's meshes, by label number. A mesh whose skin or skeleton failed to import is left out, never
         * imported unskinned. FBX and OBJ skinned meshes are still left out.
         */
        std::vector<AssetHandle<Mesh>> meshes;
        /** @brief The model's materials, by label number. */
        std::vector<AssetHandle<Material>> materials;
        /** @brief The images its materials use, by label number. One that failed to load is a 1x1 white image. */
        std::vector<AssetHandle<Image>> images;
        /** @brief The model's skeletons, by label number. */
        std::vector<AssetHandle<Skeleton>> skeletons;
        /**
         * @brief Each skeleton's owner, as Model::skeletons is numbered: the index in Model::nodes of the node whose
         * entity carries the skeleton's pose (its skeleton entity), an ordinary node or a synthetic owner.
         */
        std::vector<std::int32_t> skeletonOwners;
        /** @brief The model's clips, by label number. */
        std::vector<AssetHandle<Clip>> clips;
    };
} // namespace Fenrir
