#include "FenrirRagdollScripting/RagdollScripting.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRagdoll/Builder.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "FenrirRagdoll/Description.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "Generated/FenrirRagdollNatives.hpp"
#include "RagdollScriptingState.hpp"

// Everything C# sees of ragdolls is generated (api/FenrirRagdoll.binding.toml): the components' and shared structs'
// mappings, checked against the Fenrir.Ragdoll contract whenever a script assembly loads, and from the native functions
// below (described into api/FenrirRagdoll.natives.json) their C# declarations, RagdollNative, the generated methods on
// the Ragdoll mirror, and this registration of them all.
namespace Fenrir
{
    namespace
    {
        // The entity `id` names in the active scene now, or none if it is not alive.
        Entity HandleIn(App& app, std::uint32_t id)
        {
            EntityList& entities = app.GetActiveScene().GetEntityList();
            return entities.HasEntity(id) ? entities.GetEntity(id) : Entity{};
        }

        // The entities a Build handed out, as their ids: alive at once, built onto at playback.
        struct Reserved
        {
            std::uint32_t parent = Entity::Null;
            std::vector<std::uint32_t> bodies;
            std::vector<std::uint32_t> joints;
        };

        // Builds `description` onto the reserved entities at playback, or, when FenrirRagdoll refuses there (the
        // skeleton died or was given a Ragdoll since), destroys them and logs why.
        void BuildAtPlayback(App& app, std::uint32_t skeletonId, const RagdollDescription& description,
                             const Reserved& reserved)
        {
            SpawnedRagdoll into;
            into.parent = HandleIn(app, reserved.parent);
            for (const std::uint32_t body : reserved.bodies)
            {
                into.bodies.push_back(HandleIn(app, body));
            }
            for (const std::uint32_t joint : reserved.joints)
            {
                into.joints.push_back(HandleIn(app, joint));
            }
            Scene& scene = app.GetActiveScene();
            const SpawnedRagdoll built = BuildRagdoll(app, scene, HandleIn(app, skeletonId), description, into);
            if (built.error.empty())
            {
                return;
            }
            app.Logger()->Warn("[ragdoll scripting] RagdollNative.Build of entity {} at playback built nothing, and "
                               "its entities are destroyed: {}",
                               skeletonId, built.error);
            EntityList& entities = scene.GetEntityList();
            const auto destroy = [&](const Entity& entity) {
                if (entity.IsValid())
                {
                    entities.DestroyEntity(entity);
                }
            };
            for (const Entity& joint : into.joints)
            {
                destroy(joint);
            }
            for (const Entity& body : into.bodies)
            {
                destroy(body);
            }
            destroy(into.parent);
        }
    } // namespace

    // The native functions behind Fenrir.Ragdoll's Ragdoll statics, each named as C# calls it. tools/describe-api reads
    // them from this file, in this order, and Iris.Bindings generates their C# declarations
    // (Fenrir.Ragdoll.RagdollNative) and their registration from that. A pointer parameter is `in` from C# when const,
    // `out` when its doc marks it an output (`[out]`), and a span when the std::int32_t count follows it. Each trusts
    // the declaration (ADR-0045): an `in` or `out` pointer is never null, and the registration zeroes each single
    // output before the native runs. None needs a physics world: the components are written, and without a world
    // nothing moves (the glue's rule). Not in an anonymous namespace: the generated natives header declares them.
    namespace RagdollNatives
    {
        /**
         * @brief Derives a humanoid ragdoll description from the skeleton entity's skeleton asset and `bones` (C++'s
         * DescribeHumanoid, whose documentation gives the parts, capsules, masses and joints), and keeps it for the
         * active scene until Build takes it or the scene ends.
         * @param[in] skeleton The skeleton entity, whose SkeletonPose names the asset.
         * @param[in] bones The humanoid bone map: the bone of each slot, by engine name.
         * @param totalMass The ragdoll's mass in kilograms, shared out over the parts.
         * @return The description's handle; no description (id 0) when the entity is not a Ready skeleton entity or the
         * bone map is refused, warning once.
         */
        RagdollDescriptionHandle DescribeHumanoid(const CallContext& context, RagdollScripting& ragdolls,
                                                  const Entity* skeleton, const HumanoidBones* bones, float totalMass)
        {
            const std::optional<Entity> ready = ragdolls.ReadyOf(context, *skeleton);
            if (!ready)
            {
                return {};
            }
            const NativeGuard guard(context, ragdolls.warnings);
            const AssetStore* store = context.GetApp().TryGetResource<AssetStore>();
            const SkeletonData* data =
                store == nullptr ? nullptr : store->TryGet(ready->GetComponent<SkeletonPose>().skeleton);
            if (data == nullptr)
            {
                guard.Warn(std::string(RagdollScripting::NotReady));
                return {};
            }
            RagdollDescription description = Fenrir::DescribeHumanoid(*data, *bones, totalMass);
            if (!description.error.empty())
            {
                guard.Warn(RagdollScripting::Refused(description.error));
                return {};
            }
            return ragdolls.Keep(context, std::move(description));
        }

        /**
         * @brief How many parts the description has: how many body entities Build spawns from it (and one fewer
         * joint), to size its buffers.
         * @return The part count; 0 for no description or one the active scene does not hold.
         * @facade Ragdoll.PartCount
         */
        std::int32_t PartCount(const CallContext& context, RagdollScripting& ragdolls,
                               RagdollDescriptionHandle description)
        {
            const RagdollDescription* found = ragdolls.Find(context, description);
            return found == nullptr ? 0 : static_cast<std::int32_t>(found->parts.size());
        }

        /**
         * @brief Builds the description as the skeleton entity's ragdoll (C++'s BuildRagdoll), and drops the handle.
         * The entities are alive at once and written to the caller's buffers: the parent entity they sit under, one
         * body per part in the description's order (the root part's first), and one joint per part but the root. Their
         * components, and the skeleton entity's Ragdoll, are added when the calling system returns: a structural
         * change. A refusal FenrirRagdoll makes only then (the skeleton entity died, or got a Ragdoll, since) destroys
         * them, logging why: so a second Build of one skeleton in the same system passes here and is destroyed at
         * playback. Build once per skeleton per system.
         * @param[in] skeleton The skeleton entity the description was derived for.
         * @param bodies Where to write the body entities: at least PartCount of them.
         * @param joints Where to write the joint entities: at least PartCount less one.
         * @param[out] parent The entity the bodies and joints sit under; no entity when nothing was built.
         * @return Whether it was built: false, keeping the handle, when the entity is not a Ready skeleton entity,
         * already has a Ragdoll or a buffer is too small; false for no description or a stale handle; each warning
         * once but no description, which DescribeHumanoid already warned about.
         * @facade Ragdoll.Build
         */
        bool Build(const CallContext& context, RagdollScripting& ragdolls, const Entity* skeleton,
                   RagdollDescriptionHandle description, Entity* bodies, std::int32_t bodyCapacity, Entity* joints,
                   std::int32_t jointCapacity, Entity* parent)
        {
            if (description.id == 0)
            {
                return false;
            }
            const NativeGuard guard(context, ragdolls.warnings);
            const RagdollDescription* found = ragdolls.Find(context, description);
            if (found == nullptr)
            {
                guard.Warn(std::string(RagdollScripting::StaleDescription));
                return false;
            }
            const std::optional<Entity> ready = ragdolls.ReadyOf(context, *skeleton);
            if (!ready)
            {
                return false;
            }
            if (ready->HasComponent<Ragdoll>())
            {
                guard.Warn(std::string(RagdollScripting::AlreadyBuilt));
                return false;
            }
            const std::size_t parts = found->parts.size();
            if (bodyCapacity < 0 || jointCapacity < 0 || static_cast<std::size_t>(bodyCapacity) < parts ||
                static_cast<std::size_t>(jointCapacity) + 1 < parts)
            {
                guard.Warn(std::string(RagdollScripting::BuffersTooSmall));
                return false;
            }

            const ChangeBuffer changes = context.Changes();
            EntityList& entities = context.Entities();
            Reserved reserved;
            reserved.parent = changes.CreateEntity();
            *parent = entities.HandleOf(reserved.parent);
            for (std::size_t i = 0; i < parts; ++i)
            {
                reserved.bodies.push_back(changes.CreateEntity());
                bodies[i] = entities.HandleOf(reserved.bodies.back());
            }
            for (std::size_t i = 1; i < parts; ++i)
            {
                reserved.joints.push_back(changes.CreateEntity());
                joints[i - 1] = entities.HandleOf(reserved.joints.back());
            }
            // Shared, since a recorded change is copied: the description goes with the handle.
            auto taken = std::make_shared<const RagdollDescription>(ragdolls.Take(context, description));
            changes.RecordCall([skeletonId = ready->GetId(), taken, reserved = std::move(reserved)](App& app) {
                BuildAtPlayback(app, skeletonId, *taken, reserved);
            });
            return true;
        }

        /**
         * @brief Removes the skeleton entity's ragdoll (C++'s DestroyRagdoll) when the calling system returns: the
         * entities Build spawned, the glue's pose override entries and the Ragdoll. A structural change, so the
         * ragdoll is there until then.
         * @param[in] skeleton The skeleton entity.
         * @return Whether it was recorded: false, with nothing to remove, for an entity with no Ragdoll (silently, as
         * C++'s does nothing; so also right after a Build in the same system, whose Ragdoll is not added yet), or one
         * that is not alive (warning once).
         * @facade Ragdoll.Destroy
         */
        bool Destroy(const CallContext& context, RagdollScripting& ragdolls, const Entity* skeleton)
        {
            const NativeGuard guard(context, ragdolls.warnings);
            const std::optional<Entity> alive = guard.Alive(*skeleton);
            if (!alive || !alive->HasComponent<Ragdoll>())
            {
                return false;
            }
            context.Changes().RecordCall([id = alive->GetId()](App& app) {
                const Entity destroyed = HandleIn(app, id);
                if (destroyed.IsValid())
                {
                    DestroyRagdoll(app.GetActiveScene(), destroyed);
                }
            });
            return true;
        }

        /**
         * @brief Clears the pose override entries of the skeleton entity's ragdoll at once (C++'s
         * Ragdolls::ClearOverrides): those on its bodies' bones and on the bones of bodies destroyed since the glue
         * wrote them. Game code's entries on other bones, and every override weight, stay. It works after the Ragdoll
         * is removed, while its bodies still name the entity. While a body simulates the glue writes its entry again
         * next frame.
         * @param[in] skeleton The skeleton entity.
         * @return Whether there was a ragdoll to clear: false for an entity with no Ragdoll and no body naming it, or
         * one that is not alive (warning once).
         * @facade Ragdoll.ClearOverrides
         */
        bool ClearOverrides(const CallContext& context, RagdollScripting& ragdolls, const Entity* skeleton)
        {
            const NativeGuard guard(context, ragdolls.warnings);
            const std::optional<Entity> alive = guard.Alive(*skeleton);
            return alive && Ragdolls::ClearOverrides(context.GetApp().GetActiveScene(), *alive);
        }
    } // namespace RagdollNatives

    Fenrir::ScriptingExtension Ragdolls::ScriptingExtension()
    {
        return Generated::MakeRagdollExtension(std::make_shared<RagdollScripting>());
    }
} // namespace Fenrir
