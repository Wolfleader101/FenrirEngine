#include "FenrirRendererScripting/RendererScripting.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirRendererScripting/RendererMappings.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

namespace Fenrir
{
    namespace
    {
        // A warning logged once for each place in this file that notices a call doing nothing, and each reason, in
        // debug builds only.
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            static std::mutex mutex;
            static std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[renderer scripting] {} (warned once)", message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
        }

        // Whether the app has a renderer; warns once for the caller's call site if not.
        bool HasRenderer(const CallContext& context, std::string_view function,
                         std::source_location where = std::source_location::current())
        {
            if (Renderer::IsInstalled(context.GetApp()))
            {
                return true;
            }
            WarnOnce(context,
                     std::string(function) + ": the app has no renderer (FenrirRenderer is not installed), so it does "
                                             "nothing",
                     where);
            return false;
        }

        // The entity `id` of the active scene if it is alive; warns once for the caller's call site if not.
        std::optional<Entity> Alive(const CallContext& context, std::uint32_t id, std::string_view function,
                                    std::source_location where = std::source_location::current())
        {
            EntityList& entities = context.Entities();
            if (entities.HasEntity(id))
            {
                return entities.GetEntity(id);
            }
            WarnOnce(context, std::string(function) + ": the entity is not alive, so it does nothing", where);
            return std::nullopt;
        }

        // The camera entity `id` if the app has a renderer and it is alive with a Camera; warns once if not.
        std::optional<Entity> CameraFor(const CallContext& context, std::uint32_t id, std::string_view function,
                                        std::source_location where = std::source_location::current())
        {
            if (!HasRenderer(context, function, where))
            {
                return std::nullopt;
            }
            std::optional<Entity> camera = Alive(context, id, function, where);
            if (camera && !camera->HasComponent<Camera>())
            {
                WarnOnce(context, std::string(function) + ": the entity has no Camera, so it does nothing", where);
                return std::nullopt;
            }
            return camera;
        }

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

        // The native functions, named apart from Renderer's own functions.
        namespace Native
        {
            bool ScreenPointToRay(const CallContext& context, std::uint32_t camera, const Math::Vec2* point,
                                  Math::Ray* ray)
            {
                const std::optional<Entity> entity = CameraFor(context, camera, "Renderer.ScreenPointToRay");
                return entity && point != nullptr &&
                       Write(Renderer::ScreenPointToRay(context.GetApp(), *entity, *point), ray);
            }

            bool WorldToScreenPoint(const CallContext& context, std::uint32_t camera, const Math::Vec3* point,
                                    Math::Vec2* screenPoint)
            {
                const std::optional<Entity> entity = CameraFor(context, camera, "Renderer.WorldToScreenPoint");
                return entity && point != nullptr &&
                       Write(Renderer::WorldToScreenPoint(context.GetApp(), *entity, *point), screenPoint);
            }

            std::uint32_t CameraAt(const CallContext& context, std::uint32_t window, const Math::Vec2* point)
            {
                if (!HasRenderer(context, "Renderer.CameraAt") || point == nullptr)
                {
                    return Entity::Null;
                }
                // A null Entity's id is Entity::Null, C#'s Entity.None.
                return Renderer::CameraAt(context.GetApp(), WindowId{window}, *point).GetId();
            }

            bool GetWorldBounds(const CallContext& context, std::uint32_t entity, Math::AABB* bounds)
            {
                if (!HasRenderer(context, "Renderer.GetWorldBounds"))
                {
                    return false;
                }
                const std::optional<Entity> alive = Alive(context, entity, "Renderer.GetWorldBounds");
                return alive && Write(Renderer::GetWorldBounds(context.GetApp(), *alive), bounds);
            }

            bool Stats(const CallContext& context, RendererStats* stats)
            {
                const bool installed = HasRenderer(context, "Renderer.Stats");
                if (stats != nullptr)
                {
                    *stats = Renderer::Stats(context.GetApp());
                }
                return installed;
            }

            std::uint32_t SpawnModel(const CallContext& context, std::uint32_t modelIndex,
                                     std::uint32_t modelGeneration, std::uint32_t parent)
            {
                AssetStore* store = context.GetApp().TryGetResource<AssetStore>();
                if (store == nullptr)
                {
                    WarnOnce(context, "Renderer.SpawnModel: the app has no asset store (FenrirAssets is not "
                                      "installed), so it does nothing");
                    return Entity::Null;
                }
                const AssetHandle<Model> model{modelIndex, modelGeneration};
                if (!store->IsValid(model))
                {
                    WarnOnce(context, "Renderer.SpawnModel: the model handle is empty or stale, so it does nothing");
                    return Entity::Null;
                }
                const bool parented = parent != Entity::Null;
                if (parented && !Alive(context, parent, "Renderer.SpawnModel"))
                {
                    return Entity::Null;
                }
                // C++'s SpawnModel makes the tree, in a scene of its own, so that there is one place that says what a
                // spawned model is; the tree is then recorded into the active scene, which a native function may not
                // change directly (ADR-0029).
                Scene scratch("Renderer.SpawnModel");
                const Entity root = Fenrir::SpawnModel(*store, scratch, model);
                if (!root.IsValid())
                {
                    return Entity::Null;
                }
                return Record(context.Changes(), root,
                              parented ? std::optional<std::uint32_t>(parent) : std::optional<std::uint32_t>());
            }
        } // namespace Native
    } // namespace

    Fenrir::ScriptingExtension Renderer::ScriptingExtension()
    {
        const std::string renderer = "Fenrir.Renderer.RendererNative.";
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirRenderer";
        extension.sharedAssemblies = {"Fenrir.Renderer.dll"};
        extension.functions = {
            NativeFunction::Make<&Native::ScreenPointToRay>(renderer + "ScreenPointToRay"),
            NativeFunction::Make<&Native::WorldToScreenPoint>(renderer + "WorldToScreenPoint"),
            NativeFunction::Make<&Native::CameraAt>(renderer + "CameraAt", Entity::Null),
            NativeFunction::Make<&Native::GetWorldBounds>(renderer + "GetWorldBounds"),
            NativeFunction::Make<&Native::Stats>(renderer + "Stats"),
            NativeFunction::Make<&Native::SpawnModel>(renderer + "SpawnModel", Entity::Null),
        };
        extension.components = {
            MakeComponent<MeshRenderer>("Fenrir.Renderer.MeshRenderer"),
            MakeComponent<Camera, Detail::CameraLayout>("Fenrir.Renderer.Camera"),
            MakeComponent<DirectionalLight>("Fenrir.Renderer.DirectionalLight"),
        };
        extension.resources = {
            MakeSceneResource<AmbientLight>("Fenrir.Renderer.AmbientLight"),
            MakeSceneResource<Sky, Detail::SkyLayout>("Fenrir.Renderer.Sky"),
        };
        return extension;
    }
} // namespace Fenrir
