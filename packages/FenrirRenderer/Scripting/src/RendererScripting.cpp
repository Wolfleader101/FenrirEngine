#include "FenrirRendererScripting/RendererScripting.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
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
        // scene with its Name, Transform and MeshRenderer, `source` a child of `parent` if there is one. Children are
        // parented in the order `source` holds them, so the tree is the one the scratch scene holds.
        std::uint32_t Record(const ChangeBuffer& changes, const Entity& source, std::optional<std::uint32_t> parent)
        {
            const std::uint32_t entity = changes.CreateEntity();
            (void)changes.AddComponent<Name>(entity, source.GetComponent<Name>());
            (void)changes.AddComponent<Transform>(entity, source.GetComponent<Transform>());
            if (source.HasComponent<MeshRenderer>())
            {
                (void)changes.AddComponent<MeshRenderer>(entity, source.GetComponent<MeshRenderer>());
            }
            if (parent)
            {
                (void)changes.AddChild(*parent, entity);
            }
            source.ForEachChild([&](const Entity& child) { (void)Record(changes, child, entity); });
            return entity;
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
         * @brief Spawns `model` into the active scene as C++'s SpawnModel does, under `parent` unless it is no entity:
         * a root named after the file holding one entity per node, each mesh a MeshRenderer. The root is usable at once
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
            // directly (ADR-0029).
            Scene scratch(std::string(context.Native()));
            const Entity spawned = Fenrir::SpawnModel(*store, scratch, *model);
            if (!spawned.IsValid())
            {
                return false;
            }
            *root = context.Entities().HandleOf(
                Record(context.Changes(), spawned,
                       under ? std::optional<std::uint32_t>(under->GetId()) : std::optional<std::uint32_t>()));
            return true;
        }
    } // namespace RendererNatives

    Fenrir::ScriptingExtension Renderer::ScriptingExtension()
    {
        return Generated::MakeRendererExtension(std::make_shared<RendererScripting>());
    }
} // namespace Fenrir
