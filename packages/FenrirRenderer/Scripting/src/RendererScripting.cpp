#include "FenrirRendererScripting/RendererScripting.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirAssets/SpawnNodes.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Frustum.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "Generated/FenrirRendererNatives.hpp"
#include "RendererScriptingState.hpp"

// Everything C# sees of the renderer is generated (api/FenrirRenderer.binding.toml): the components', resources' and
// shared struct's mappings, checked against the Fenrir.Renderer contract whenever a script assembly loads, and from the
// native functions below (described into api/FenrirRenderer.natives.json) their C# declarations, RendererNative, and
// this registration of them all.
namespace Fenrir::Generated
{
    void RegisterRenderer(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    namespace
    {
        // Calls the facade's `setter` on the app's renderer with `arguments` and says whether there was one to call:
        // the one shape of every setter native, whose C# result is whether the app has a renderer.
        template <typename... Args>
        bool SetOnRenderer(const CallContext& context, RendererScripting& renderer, void (*setter)(App&, Args...),
                           Args... arguments)
        {
            if (!renderer.HasRenderer(context))
            {
                return false;
            }
            setter(context.GetApp(), arguments...);
            return true;
        }

        // Writes `value` through `out` if there is one, and says whether there was.
        template <typename V>
        bool Write(const std::optional<V>& value, V* out)
        {
            if (value.has_value())
            {
                *out = *value;
                return true;
            }
            return false;
        }

        // Records `source`, an entity of a scratch scene, and its children, depth first, as new entities of the active
        // scene with its Name, Transform, MeshRenderer, SkinnedMesh, PointLight and SpotLight, `source` a child of
        // `parent` if there is one, noting each new entity's id under its scratch id in `made`. Children are parented
        // in the order `source` holds them, so the tree is the one the scratch scene holds. A SkinnedMesh names its
        // skeleton entity by the new id: the skeleton's owner is the mesh's ancestor, so it was made first.
        std::uint32_t Record(const ChangeBuffer& changes, EntityList& entities, const Entity& source,
                             std::optional<std::uint32_t> parent,
                             std::unordered_map<std::uint32_t, std::uint32_t>& made)
        {
            const std::uint32_t entity = changes.CreateEntity();
            made[source.GetId()] = entity;
            (void)changes.AddComponent<Name>(entity, source.GetComponent<Name>());
            (void)changes.AddComponent<Transform>(entity, source.GetComponent<Transform>());
            if (source.HasComponent<MeshRenderer>())
            {
                (void)changes.AddComponent<MeshRenderer>(entity, source.GetComponent<MeshRenderer>());
            }
            if (source.HasComponent<SkinnedMesh>())
            {
                const auto skeleton = made.find(source.GetComponent<SkinnedMesh>().skeleton.GetId());
                const Entity remapped = skeleton != made.end() ? entities.HandleOf(skeleton->second) : Entity{};
                (void)changes.AddComponent<SkinnedMesh>(entity, SkinnedMesh{remapped});
            }
            if (source.HasComponent<PointLight>())
            {
                (void)changes.AddComponent<PointLight>(entity, source.GetComponent<PointLight>());
            }
            if (source.HasComponent<SpotLight>())
            {
                (void)changes.AddComponent<SpotLight>(entity, source.GetComponent<SpotLight>());
            }
            if (parent)
            {
                (void)changes.AddChild(*parent, entity);
            }
            source.ForEachChild([&](const Entity& child) { (void)Record(changes, entities, child, entity, made); });
            return entity;
        }

        // Gives the recorded tree its skeletons at playback, through the skinning source as C++'s SpawnModel does
        // (FenrirSkeleton's SpawnSkeletons: a SkeletonPose on each owner, bone attachments below bones), on the new
        // entities: each spawned node's by the id `made` holds for its scratch entity. Nothing without a skinning
        // source, whose absence the scratch spawn already logged.
        void RecordSkeletons(const ChangeBuffer& changes, AssetHandle<Model> model, const SpawnedNodes& scratch,
                             const std::unordered_map<std::uint32_t, std::uint32_t>& made)
        {
            std::vector<std::optional<std::uint32_t>> nodes;
            nodes.reserve(scratch.nodes.size());
            for (const Entity& node : scratch.nodes)
            {
                const auto entity = node.IsValid() ? made.find(node.GetId()) : made.end();
                nodes.push_back(entity != made.end() ? std::optional(entity->second) : std::nullopt);
            }
            const std::uint32_t root = made.at(scratch.root.GetId());
            changes.RecordCall([model, root, nodes = std::move(nodes)](App& app) {
                const SkinningSourceRegistration* skinning = app.TryGetResource<SkinningSourceRegistration>();
                if (skinning == nullptr)
                {
                    return;
                }
                Scene& scene = app.GetActiveScene();
                EntityList& entities = scene.GetEntityList();
                const auto handle = [&entities](std::uint32_t id) {
                    return entities.HasEntity(id) ? entities.GetEntity(id) : Entity{};
                };
                SpawnedNodes spawned;
                spawned.root = handle(root);
                for (const std::optional<std::uint32_t>& node : nodes)
                {
                    spawned.nodes.push_back(node ? handle(*node) : Entity{});
                }
                skinning->source->SpawnSkeletons(scene, model, spawned);
            });
        }
    } // namespace

    // The native functions behind Fenrir.Renderer's Renderer, each its one call behind the native guard (ADR-0045: its
    // `in` and `out` pointers are never null, and the registration zeroes each single output before it runs).
    // tools/describe-api reads them from this file, in this order, and Iris.Bindings generates their C# declarations
    // (Fenrir.Renderer.RendererNative) and their registration from that. With no renderer (or store) in the app, an
    // entity that is not alive or has no Camera, or a stale model, each does nothing and returns false or no entity,
    // and warns once per call site in a debug build under its own name, which the call context carries. Named apart
    // from Renderer's own functions, and not in an anonymous namespace: the generated natives header declares them.
    namespace RendererNatives
    {
        /**
         * @brief The ray from `camera` through `point`, in window pixels from the top-left of its target.
         * @param[out] ray The ray; the default ray when there is none.
         * @return Whether there is a ray: false with no renderer, no such camera, or a point outside the camera's
         * viewport.
         * @facade Renderer.ScreenPointToRay
         */
        bool ScreenPointToRay(const CallContext& context, RendererScripting& renderer, const Entity* camera,
                              const Math::Vec2* point, Math::Ray* ray)
        {
            const std::optional<Entity> entity = renderer.CameraFor(context, *camera);
            return entity && Write(Renderer::ScreenPointToRay(context.GetApp(), *entity, *point), ray);
        }

        /**
         * @brief Where `point`, in world space, appears on `camera`'s target, in window pixels from its top-left.
         * @param[out] screenPoint The point; zero when it does not appear.
         * @return Whether it appears: false with no renderer, no such camera, or a point behind the camera.
         * @facade Renderer.WorldToScreenPoint
         */
        bool WorldToScreenPoint(const CallContext& context, RendererScripting& renderer, const Entity* camera,
                                const Math::Vec3* point, Math::Vec2* screenPoint)
        {
            const std::optional<Entity> entity = renderer.CameraFor(context, *camera);
            return entity && Write(Renderer::WorldToScreenPoint(context.GetApp(), *entity, *point), screenPoint);
        }

        /**
         * @brief The camera drawn last at `point` of `window`, in window pixels from its top-left: the highest-order
         * enabled camera in the active scene whose viewport holds it.
         * @param[out] camera The camera entity; no entity (the default Entity, C#'s Entity.None) when there is none.
         * @return Whether there is one: false when there is none or no renderer.
         * @facade Renderer.CameraAt returns camera
         */
        bool CameraAt(const CallContext& context, RendererScripting& renderer, WindowId window, const Math::Vec2* point,
                      Entity* camera)
        {
            if (!renderer.HasRenderer(context))
            {
                return false;
            }
            const Entity found = Renderer::CameraAt(context.GetApp(), window, *point);
            if (!found.IsValid())
            {
                return false;
            }
            *camera = found;
            return true;
        }

        /**
         * @brief The world-space box around the drawn meshes of `entity` and its children, now.
         * @param[out] bounds The box; the default box when there is none.
         * @return Whether there is one: false with no renderer, an entity not alive, or no mesh to bound (no
         * MeshRenderer, or a mesh that does not resolve).
         * @facade Renderer.GetWorldBounds
         */
        bool GetWorldBounds(const CallContext& context, RendererScripting& renderer, const Entity* entity,
                            Math::AABB* bounds)
        {
            if (!renderer.HasRenderer(context))
            {
                return false;
            }
            const NativeGuard guard(context, renderer.warnings);
            const std::optional<Entity> alive = guard.Alive(*entity);
            return alive && Write(Renderer::GetWorldBounds(context.GetApp(), *alive), bounds);
        }

        /**
         * @brief What the renderer did in the last drawn frame; zeros before the first.
         * @param[out] stats The counts; zero with no renderer.
         * @return Whether the app has a renderer.
         * @facade Renderer.Stats
         */
        bool Stats(const CallContext& context, RendererScripting& renderer, RendererStats* stats)
        {
            if (!renderer.HasRenderer(context))
            {
                return false;
            }
            *stats = Renderer::Stats(context.GetApp());
            return true;
        }

        /**
         * @brief Sets how many SpotLights may cast shadows from the next frame (RendererConfig::spotShadowBudget,
         * ADR-0050): the nearest to any camera each frame; the rest light unshadowed. 0 shadows none.
         * @return Whether the app has a renderer.
         * @facade Renderer.SetSpotShadowBudget
         */
        bool SetSpotShadowBudget(const CallContext& context, RendererScripting& renderer, std::uint32_t budget)
        {
            return SetOnRenderer(context, renderer, &Renderer::SetSpotShadowBudget, budget);
        }

        /**
         * @brief Sets how many PointLights may cast shadows from the next frame (RendererConfig::pointShadowBudget,
         * ADR-0050): the nearest to any camera each frame; the rest light unshadowed. 0 shadows none.
         * @return Whether the app has a renderer.
         * @facade Renderer.SetPointShadowBudget
         */
        bool SetPointShadowBudget(const CallContext& context, RendererScripting& renderer, std::uint32_t budget)
        {
            return SetOnRenderer(context, renderer, &Renderer::SetPointShadowBudget, budget);
        }

        /**
         * @brief Turns the GPU frustum test (RendererConfig::gpuCulling, ADR-0057) on or off from the next frame, as
         * Renderer::SetGpuCulling does: off, every cull pass keeps every candidate at level 0 and runs no occlusion
         * phase, for comparison; the coarse cull of whole entities on the CPU stays on. Scripting never reaches
         * RendererConfig, so this is C#'s way to the switch.
         * @return Whether the app has a renderer.
         * @facade Renderer.SetGpuCulling
         */
        bool SetGpuCulling(const CallContext& context, RendererScripting& renderer, bool on)
        {
            return SetOnRenderer(context, renderer, &Renderer::SetGpuCulling, on);
        }

        /**
         * @brief Turns occlusion culling (RendererConfig::occlusionCulling, ADR-0057) on or off from the next frame,
         * as Renderer::SetOcclusionCulling does: off, each camera's cull pass runs once and keeps every copy inside
         * its frustum, for comparison. Shadow views never cull by occlusion.
         * @return Whether the app has a renderer.
         * @facade Renderer.SetOcclusionCulling
         */
        bool SetOcclusionCulling(const CallContext& context, RendererScripting& renderer, bool on)
        {
            return SetOnRenderer(context, renderer, &Renderer::SetOcclusionCulling, on);
        }

        /**
         * @brief Freezes culling from the next frame, or thaws it, as Renderer::SetCullingFrozen does (ADR-0057):
         * frozen, each camera keeps the frustum and position it has in that frame and culls against them however it
         * moves after, and the Debug stage draws each frozen frustum's edges, so flying out of it shows what was kept.
         * Freezing while frozen keeps the first frame's volumes.
         * @return Whether the app has a renderer.
         * @facade Renderer.SetCullingFrozen
         */
        bool SetCullingFrozen(const CallContext& context, RendererScripting& renderer, bool on)
        {
            return SetOnRenderer(context, renderer, &Renderer::SetCullingFrozen, on);
        }

        /**
         * @brief Sets both level-of-detail thresholds from the next frame, as Renderer::SetLodSelection does
         * (RendererConfig::lodErrorPixels and minProjectedPixels, ADR-0057): each copy of a mesh with levels draws at
         * the coarsest level whose error, scaled by the copy's largest axis scale and projected at its distance from
         * the camera, is under `lodErrorPixels` pixels, and a copy whose bounding sphere's radius projects under
         * `minProjectedPixels` is dropped (0: never). Scripting never reaches RendererConfig, so this is C#'s way to
         * both.
         * @param lodErrorPixels The error, in pixels, a drawn level may show; 1 by default.
         * @param minProjectedPixels The radius, in pixels, under which a copy is dropped; 0 by default.
         * @return Whether the app has a renderer.
         * @facade Renderer.SetLodSelection
         */
        bool SetLodSelection(const CallContext& context, RendererScripting& renderer, float lodErrorPixels,
                             float minProjectedPixels)
        {
            return SetOnRenderer(context, renderer, &Renderer::SetLodSelection, lodErrorPixels, minProjectedPixels);
        }

        /**
         * @brief The volume `camera` sees now (ADR-0057): the planes the renderer culls by, in world space from the
         * camera's world matrix and its target's size, for a game that culls or streams its own entities. Five planes
         * for a perspective camera, whose far plane is at infinity, six for an orthographic one.
         * @param[out] frustum The frustum; one with no planes, which everything is inside, when there is none.
         * @return Whether there is one: false with no renderer, no such camera, or a camera that draws nothing.
         * @facade Renderer.GetFrustum
         */
        bool GetFrustum(const CallContext& context, RendererScripting& renderer, const Entity* camera,
                        Math::Frustum* frustum)
        {
            const std::optional<Entity> entity = renderer.CameraFor(context, *camera);
            return entity && Write(Renderer::GetFrustum(context.GetApp(), *entity), frustum);
        }

        /**
         * @brief Spawns `model` into the active scene as C++'s SpawnModel does, under `parent` unless it is no entity:
         * a root named after the file holding one entity per node, each mesh a MeshRenderer, each skinned mesh with a
         * SkinnedMesh naming its skeleton's owner, and with FenrirSkeleton installed a SkeletonPose on each owner and
         * bone attachments below bones, given when the tree plays back. The root is usable at once
         * (alive, so it can be given components or parented in the same call); the tree is made, as every structural
         * change is, when the system returns. It needs no renderer in the app.
         * @param model The model's handle.
         * @param parent The entity to spawn under; no entity (Entity.None in C#) for none. A parent that is not alive
         * spawns nothing.
         * @param[out] root The model's root entity; no entity (the default Entity, C#'s Entity.None) when nothing was
         * spawned: an empty or stale model handle, no asset store, or a parent not alive.
         * @return Whether it spawned: false when the app has no store, the model is stale or the parent is not alive.
         * @facade Renderer.SpawnModel returns root
         */
        bool SpawnModel(const CallContext& context, RendererScripting& renderer, const AssetHandle<Model>* model,
                        const Entity* parent, Entity* root)
        {
            const NativeGuard guard(context, renderer.warnings);
            // The handle rule is Assets' (AssetResolve.hpp), so the words are too, under this native's name.
            AssetStore* store = Resolve<Model>(guard, *model);
            if (store == nullptr)
            {
                return false;
            }
            const bool parented = parent->GetId() != Entity::Null;
            const std::optional<Entity> under = parented ? guard.Alive(*parent) : std::nullopt;
            if (parented && !under)
            {
                return false;
            }
            // C++'s SpawnModel makes the tree, in a scene of its own, so that there is one place that says what a
            // spawned model is; the tree is then recorded into the active scene, which a native function may not change
            // directly (ADR-0029), and its skeletons given to the recorded entities when it plays back.
            Scene scratch(std::string(context.Native()));
            const SpawnedNodes spawned = Fenrir::SpawnModelNodes(context.GetApp(), scratch, *model);
            if (!spawned.root.IsValid())
            {
                return false;
            }
            const ChangeBuffer changes = context.Changes();
            std::unordered_map<std::uint32_t, std::uint32_t> made;
            *root = context.Entities().HandleOf(
                Record(changes, context.Entities(), spawned.root,
                       under ? std::optional<std::uint32_t>(under->GetId()) : std::optional<std::uint32_t>(), made));
            RecordSkeletons(changes, *model, spawned, made);
            return true;
        }
    } // namespace RendererNatives

    Fenrir::ScriptingExtension Renderer::ScriptingExtension()
    {
        return Generated::MakeRendererExtension(std::make_shared<RendererScripting>());
    }
} // namespace Fenrir
