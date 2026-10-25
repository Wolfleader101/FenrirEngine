#include "FenrirRendererScripting/RendererScripting.hpp"

#include <cstdint>
#include <memory>
#include <optional>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
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
            if (value.has_value() && out != nullptr)
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

    // The native functions behind Fenrir.Renderer's Renderer, each named as C# calls it. tools/describe-api reads them
    // from this file, in this order, and Iris.Bindings generates their C# declarations (Fenrir.Renderer.RendererNative)
    // and their registration from that. With no renderer (or asset store) in the app, an entity that is not alive or
    // has no Camera, or a stale model, each does nothing and returns false or no entity, having written its output's
    // default first, and warns once per call site in a debug build. Named apart from Renderer's own functions, and not
    // in an anonymous namespace: the generated registration names them from another file.
    namespace RendererNatives
    {
        /**
         * @brief The ray from `camera` through `point`, in window pixels from the top-left of its target.
         * @param[out] ray The ray; the default ray when there is none.
         * @return Whether there is a ray: false with no renderer or no such camera.
         */
        bool ScreenPointToRay(const CallContext& context, RendererScripting& renderer, RendererScript::EntityId camera,
                              const Math::Vec2* point, Math::Ray* ray)
        {
            if (ray != nullptr)
            {
                *ray = Math::Ray{};
            }
            const std::optional<Entity> entity = renderer.CameraFor(context, camera, "Renderer.ScreenPointToRay");
            return entity && point != nullptr &&
                   Write(Renderer::ScreenPointToRay(context.GetApp(), *entity, *point), ray);
        }

        /**
         * @brief Where `point`, in world space, lands on `camera`'s target, in window pixels from its top-left.
         * @param[out] screenPoint The point; zero when it does not land.
         * @return Whether it lands: false with no renderer, no such camera, or a point behind the camera.
         */
        bool WorldToScreenPoint(const CallContext& context, RendererScripting& renderer,
                                RendererScript::EntityId camera, const Math::Vec3* point, Math::Vec2* screenPoint)
        {
            if (screenPoint != nullptr)
            {
                *screenPoint = Math::Vec2(0.0f, 0.0f);
            }
            const std::optional<Entity> entity = renderer.CameraFor(context, camera, "Renderer.WorldToScreenPoint");
            return entity && point != nullptr &&
                   Write(Renderer::WorldToScreenPoint(context.GetApp(), *entity, *point), screenPoint);
        }

        /**
         * @brief The camera whose view of `window` is under `point`, in window pixels from its top-left.
         * @return The camera entity, or no entity (C#'s Entity.None).
         * @notrunning Fenrir::Entity::Null
         */
        RendererScript::EntityId CameraAt(const CallContext& context, RendererScripting& renderer, WindowId window,
                                          const Math::Vec2* point)
        {
            if (!renderer.HasRenderer(context, "Renderer.CameraAt") || point == nullptr)
            {
                return Entity::Null;
            }
            // A null Entity's id is Entity::Null, C#'s Entity.None.
            return Renderer::CameraAt(context.GetApp(), window, *point).GetId();
        }

        /**
         * @brief The world-space box around the meshes of `entity` and its children.
         * @param[out] bounds The box; the default box when there is none.
         * @return Whether there is one: false with no renderer, an entity not alive, or no mesh to bound.
         */
        bool GetWorldBounds(const CallContext& context, RendererScripting& renderer, RendererScript::EntityId entity,
                            Math::AABB* bounds)
        {
            if (bounds != nullptr)
            {
                *bounds = Math::AABB{};
            }
            if (!renderer.HasRenderer(context, "Renderer.GetWorldBounds"))
            {
                return false;
            }
            const std::optional<Entity> alive = renderer.Alive(context, entity, "Renderer.GetWorldBounds");
            return alive && Write(Renderer::GetWorldBounds(context.GetApp(), *alive), bounds);
        }

        /**
         * @brief What the renderer did in the last frame.
         * @param[out] stats The counts; zero with no renderer.
         * @return Whether the app has a renderer.
         */
        bool Stats(const CallContext& context, RendererScripting& renderer, RendererStats* stats)
        {
            const bool installed = renderer.HasRenderer(context, "Renderer.Stats");
            if (stats != nullptr)
            {
                *stats = Renderer::Stats(context.GetApp());
            }
            return installed;
        }

        /**
         * @brief Spawns `model` into the active scene as SpawnModel does, under `parent` unless it is no entity:
         * recorded, so it takes effect when the system returns.
         * @param model The model's handle, packed as Fenrir.Assets packs one (generation high, index low).
         * @return The model's root entity, or no entity (C#'s Entity.None) with no asset store, a stale model or a
         * parent that is not alive.
         * @notrunning Fenrir::Entity::Null
         */
        RendererScript::EntityId SpawnModel(const CallContext& context, RendererScripting& renderer,
                                            std::uint64_t model, RendererScript::EntityId parent)
        {
            AssetStore* store = context.GetApp().TryGetResource<AssetStore>();
            if (store == nullptr)
            {
                renderer.WarnOnce(context, "Renderer.SpawnModel: the app has no asset store (FenrirAssets is not "
                                           "installed), so it does nothing");
                return Entity::Null;
            }
            const AssetHandle<Model> handle{static_cast<std::uint32_t>(model), static_cast<std::uint32_t>(model >> 32)};
            if (!store->IsValid(handle))
            {
                renderer.WarnOnce(context,
                                  "Renderer.SpawnModel: the model handle is empty or stale, so it does nothing");
                return Entity::Null;
            }
            const bool parented = parent != Entity::Null;
            if (parented && !renderer.Alive(context, parent, "Renderer.SpawnModel"))
            {
                return Entity::Null;
            }
            // C++'s SpawnModel makes the tree, in a scene of its own, so that there is one place that says what a
            // spawned model is; the tree is then recorded into the active scene, which a native function may not change
            // directly (ADR-0029).
            Scene scratch("Renderer.SpawnModel");
            const Entity root = Fenrir::SpawnModel(*store, scratch, handle);
            if (!root.IsValid())
            {
                return Entity::Null;
            }
            return Record(context.Changes(), root,
                          parented ? std::optional<std::uint32_t>(parent) : std::optional<std::uint32_t>());
        }
    } // namespace RendererNatives

    Fenrir::ScriptingExtension Renderer::ScriptingExtension()
    {
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirRenderer";
        extension.SetState(std::make_shared<RendererScripting>());
        extension.sharedAssemblies = {"Fenrir.Renderer.dll"};
        Generated::RegisterRenderer(extension);
        return extension;
    }
} // namespace Fenrir
