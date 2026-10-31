#pragma once

#include <array>
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
    /** @brief Khronos's Fox (assets/ATTRIBUTION.md): about 80 units tall, so drawn at FoxScale. Clips: Survey, Walk,
     * Run. */
    inline constexpr std::string_view FoxFile = "fox/Fox.gltf";
    inline constexpr float FoxScale = 0.01f;
    /**
     * @brief Quaternius's human (assets/ATTRIBUTION.md): a 71-bone skinned model with no clips of its own, and its
     * clips, one file each on the same skeleton, bound to it by bone name. Clips: idle, walk, jog, sprint, and a call
     * (the library has no wave), in QuaterniusClip's order.
     */
    inline constexpr std::string_view QuaterniusFile = "quaternius/rig-human.glb";
    inline constexpr std::array<std::string_view, 5> QuaterniusClips{
        "quaternius/idle_loop.glb", "quaternius/walk_loop.glb", "quaternius/jog_fwd_loop.glb",
        "quaternius/sprint_loop.glb", "quaternius/idle_rail_call.glb"};
    /** @brief The index into QuaterniusClips of each of the human's clips. */
    enum class QuaterniusClip : std::size_t
    {
        Idle,
        Walk,
        Run,
        Sprint,
        Call,
    };
    /** @brief Khronos's CesiumMan, untextured (assets/ATTRIBUTION.md): one walk clip. */
    inline constexpr std::string_view CesiumManFile = "cesium-man/CesiumMan.gltf";
    /**
     * @brief Kenney's animated character (assets/ATTRIBUTION.md), the FBX model: about 3.8 units tall, so drawn at
     * KenneyScale, with no clips of its own and no texture named in its file (KenneySkin is one of the pack's skins).
     * Its clips are files of their own on the same skeleton, each holding a one-frame clip and then the clip itself,
     * so KenneyClip is the clip to take from each.
     */
    inline constexpr std::string_view KenneyFile = "kenney/characterMedium.fbx";
    inline constexpr std::string_view KenneySkin = "kenney/skaterMaleA.png";
    inline constexpr std::array<std::string_view, 2> KenneyClips{"kenney/idle.fbx", "kenney/run.fbx"};
    inline constexpr std::size_t KenneyClip = 1;
    inline constexpr float KenneyScale = 0.48f;

    /** @brief A model spawned through the renderer's SpawnModel, with its skeleton entity and skinned mesh entities. */
    struct SkinnedModel
    {
        Fenrir::AssetHandle<Fenrir::Model> model;
        Fenrir::Entity root;
        /** @brief The entity carrying the SkeletonPose: the model's one skeleton's owner. */
        Fenrir::Entity skeleton;
        /** @brief The entities with a SkinnedMesh, under the skeleton entity. */
        std::vector<Fenrir::Entity> meshes;

        /** @brief Whether the model spawned with a skeleton entity. */
        [[nodiscard]] bool IsValid() const
        {
            return root.IsValid() && skeleton.IsValid();
        }

        /** @brief The model's clip `index`, or an empty handle. */
        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Clip> Clip(Fenrir::App& app, std::size_t index) const;
    };

    /**
     * @brief Loads `file` for `scene` (owned by it) and spawns it at `position`, turned `yaw` radians about +Y and
     * scaled by `scale`. An invalid SkinnedModel, logged as an error, when the file does not load or has no skeleton.
     */
    SkinnedModel SpawnSkinnedModel(Fenrir::App& app, Fenrir::Scene& scene, std::string_view file,
                                   Fenrir::Math::Vec3 position, float yaw = 0.0f, float scale = 1.0f);

    /** @brief Clip `index` of the model in `file`, loaded for `scene`; an empty handle, logged, if there is none. */
    Fenrir::AssetHandle<Fenrir::Clip> LoadClip(Fenrir::App& app, Fenrir::Scene& scene, std::string_view file,
                                               std::size_t index = 0);

    /** @brief Loads the image `file` for `scene` and makes it the base colour texture of each of the model's meshes. */
    void Paint(Fenrir::App& app, Fenrir::Scene& scene, const SkinnedModel& spawned, std::string_view file);

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
