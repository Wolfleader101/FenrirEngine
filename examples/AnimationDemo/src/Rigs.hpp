#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "FenrirAnimation/Animator.hpp"
#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class App;
    class DebugDraw;
    class Scene;
} // namespace Fenrir

// What AnimationDemo's scenes share: the stand-in models (assets/ATTRIBUTION.md), spawning them, playing their clips,
// drawing their bones and the stage they stand on.
namespace AnimationDemo
{
    /** @brief The mannequin's clips, in the order make_assets.py writes them (a clip has no name of its own). */
    enum class MannequinClip : std::size_t
    {
        Idle,
        Walk,
        Run,
        Wave,
        /** @brief A quarter-second hand flutter, the event-budget clip. */
        Flutter,
    };

    /** @brief The asset paths of the stand-in models, under the asset root. */
    inline constexpr std::string_view MannequinFile = "mannequin.gltf";
    inline constexpr std::string_view CritterFile = "critter.gltf";

    /** @brief A model spawned through the renderer's SpawnModel, with its skeleton entity and skinned mesh entities. */
    struct Rig
    {
        Fenrir::AssetHandle<Fenrir::Model> model;
        Fenrir::Entity root;
        /** @brief The entity carrying the SkeletonPose: the model's one skeleton's owner. */
        Fenrir::Entity skeleton;
        /** @brief The entities with a SkinnedMesh, under the skeleton entity. */
        std::vector<Fenrir::Entity> meshes;

        [[nodiscard]] bool IsValid() const
        {
            return root.IsValid() && skeleton.IsValid();
        }

        /** @brief The model's clip `index`, or an empty handle. */
        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Clip> Clip(Fenrir::App& app, std::size_t index) const;
    };

    /**
     * @brief Loads `file` for `scene` (owned by it) and spawns it at `position`, turned `yaw` radians about +Y. An
     * invalid Rig, logged as an error, when the file does not load or has no skeleton.
     */
    Rig SpawnRig(Fenrir::App& app, Fenrir::Scene& scene, std::string_view file, Fenrir::Math::Vec3 position,
                 float yaw = 0.0f);

    /** @brief Gives the skeleton entity an Animator playing `clip`. */
    void Play(Fenrir::Entity skeleton, Fenrir::AssetHandle<Fenrir::Clip> clip,
              Fenrir::ClipLoop loop = Fenrir::ClipLoop::Loop, float speed = 1.0f);

    /**
     * @brief Draws the skeleton entity's bones this frame through FenrirDebugDraw, on top of the meshes: a line from
     * each bone to its parent, `bones` on; small axes at each joint, with `bones`; each bone's engine name as world
     * text, with `names`.
     */
    void DrawBones(Fenrir::App& app, Fenrir::Scene& scene, Fenrir::DebugDraw& draw, Fenrir::Entity skeleton, bool bones,
                   bool names);

    /** @brief A lit floor, a directional light and ambient light: the stage every scene stands on. */
    void AddStage(Fenrir::App& app, Fenrir::Scene& scene, float width = 16.0f, float depth = 10.0f);
} // namespace AnimationDemo
