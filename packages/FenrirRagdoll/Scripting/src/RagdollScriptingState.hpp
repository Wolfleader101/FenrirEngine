#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirRagdoll/Description.hpp"
#include "FenrirRagdollScripting/RagdollDescriptionHandle.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace Fenrir
{
    namespace Detail
    {
        /**
         * @brief The descriptions C# has asked for in one scene and not yet built, by handle id. Its scene owns it,
         * through the RagdollDescriptions scene resource, so a described-but-not-built description goes when the scene
         * does. Until that resource is placed (a structural change, played back after the calling system), the
         * recorded change holds it.
         */
        struct RagdollDescriptionShelf
        {
            /** @brief The scene it belongs to, compared and never dereferenced. */
            const Scene* scene = nullptr;
            /** @brief Each description by its handle's id. */
            std::unordered_map<std::uint32_t, RagdollDescription> descriptions;
            /** @brief Whether its scene holds it now (or it was given up on, its scene gone before playback). */
            bool placed = false;
        };

        /** @brief The scene resource holding a scene's shelf: C++'s only, never mirrored. */
        struct RagdollDescriptions
        {
            std::shared_ptr<RagdollDescriptionShelf> shelf;
        };
    } // namespace Detail

    /**
     * @brief The ragdoll extension's state, which every ragdoll native function is given second: the warnings it has
     * logged, the next description id, and the shelves of descriptions whose scene does not hold them yet. Made by
     * Ragdolls::ScriptingExtension and kept by the Scripting given the extension. In a header of its own because the
     * generated natives header (Generated/FenrirRagdollNatives.hpp) declares the natives, which name it.
     *
     * Its lookups are built on the native guard (NativeGuard.hpp): each warns once per native function and message,
     * under the native's name. The texts the natives warn with are named here, one per rule, so the package's rules
     * read in one place and its rules tests name them.
     */
    struct RagdollScripting
    {
        /** @brief Warned when the entity is alive but not a Ready skeleton entity (ADR-0039): it does nothing. */
        static constexpr std::string_view NotReady = "the entity is not a Ready skeleton entity, so it does nothing";

        /** @brief Warned by Build for a handle the active scene does not hold (built, or another scene's). */
        static constexpr std::string_view StaleDescription =
            "the description handle is stale (built already, or described in another scene), so it builds nothing";

        /** @brief Warned by Build for a skeleton entity that already has a Ragdoll: one ragdoll per skeleton. */
        static constexpr std::string_view AlreadyBuilt =
            "the skeleton entity already has a Ragdoll, so it builds nothing and keeps the description";

        /** @brief Warned by Build when the caller's buffers hold fewer entities than the description spawns. */
        static constexpr std::string_view BuffersTooSmall =
            "the bodies or joints buffer is smaller than the description's parts (PartCount, and one fewer joint), so "
            "it builds nothing and keeps the description";

        /**
         * @brief Warned by DescribeHumanoid when FenrirRagdoll refuses the bone map (`error` is its reason): it
         * describes nothing.
         */
        [[nodiscard]] static std::string Refused(std::string_view error)
        {
            return "the bone map was refused (" + std::string(error) + "), so it describes nothing";
        }

        /** @brief `entity` as the active scene's handle, if it is alive there and a Ready skeleton entity. */
        std::optional<Entity> ReadyOf(const CallContext& context, const Entity& entity)
        {
            const NativeGuard guard(context, warnings);
            const std::optional<Entity> alive = guard.Alive(entity);
            if (!alive)
            {
                return std::nullopt;
            }
            if (Skeletons::GetPoseState(context.GetApp().GetActiveScene(), *alive) != PoseState::Ready)
            {
                guard.Warn(std::string(NotReady));
                return std::nullopt;
            }
            return alive;
        }

        /** @brief Keeps `description` for the active scene and gives its handle. */
        RagdollDescriptionHandle Keep(const CallContext& context, RagdollDescription description)
        {
            const std::uint32_t id = ++lastId;
            ShelfOf(context, true)->descriptions.emplace(id, std::move(description));
            return RagdollDescriptionHandle{id};
        }

        /** @brief The description `handle` names in the active scene, or null (no description, or not this scene's). */
        const RagdollDescription* Find(const CallContext& context, RagdollDescriptionHandle handle)
        {
            Detail::RagdollDescriptionShelf* shelf = handle.id == 0 ? nullptr : ShelfOf(context, false);
            if (shelf == nullptr)
            {
                return nullptr;
            }
            const auto found = shelf->descriptions.find(handle.id);
            return found == shelf->descriptions.end() ? nullptr : &found->second;
        }

        /** @brief Takes the description `handle` names out of the active scene's shelf; the handle is stale after. */
        RagdollDescription Take(const CallContext& context, RagdollDescriptionHandle handle)
        {
            Detail::RagdollDescriptionShelf* shelf = ShelfOf(context, false);
            RagdollDescription taken = std::move(shelf->descriptions.at(handle.id));
            shelf->descriptions.erase(handle.id);
            return taken;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"ragdoll scripting"};

      private:
        // The active scene's shelf: the one it holds, else the one waiting to be placed in it, else (with `make`) a
        // new one, whose placing is recorded. Null without `make` when the scene has none.
        Detail::RagdollDescriptionShelf* ShelfOf(const CallContext& context, bool make)
        {
            Scene& scene = context.GetApp().GetActiveScene();
            if (const Detail::RagdollDescriptions* held = scene.GetResource<Detail::RagdollDescriptions>())
            {
                return held->shelf.get();
            }
            std::erase_if(unplaced, [](const std::weak_ptr<Detail::RagdollDescriptionShelf>& shelf) {
                const std::shared_ptr<Detail::RagdollDescriptionShelf> live = shelf.lock();
                return live == nullptr || live->placed;
            });
            for (const std::weak_ptr<Detail::RagdollDescriptionShelf>& waiting : unplaced)
            {
                const std::shared_ptr<Detail::RagdollDescriptionShelf> live = waiting.lock();
                if (live != nullptr && live->scene == &scene)
                {
                    return live.get();
                }
            }
            if (!make)
            {
                return nullptr;
            }
            auto shelf = std::make_shared<Detail::RagdollDescriptionShelf>();
            shelf->scene = &scene;
            unplaced.push_back(shelf);
            Detail::RagdollDescriptionShelf* made = shelf.get();
            Place(context, std::move(shelf));
            return made;
        }

        // The last description id handed out; 0 is never one.
        std::uint32_t lastId = 0;

        // The shelves whose scene does not hold them yet, held weakly: until playback the recorded placing owns each,
        // and then its scene does, so nothing here keeps a description past its scene.
        std::vector<std::weak_ptr<Detail::RagdollDescriptionShelf>> unplaced;

        // Records the shelf's placing into its scene, as the scene resource that owns it from then on; until then the
        // recorded change owns it. A shelf whose scene is no longer the active one at playback is given up, and so is
        // one whose system threw (its changes dropped): its descriptions go, and their handles read as stale.
        static void Place(const CallContext& context, std::shared_ptr<Detail::RagdollDescriptionShelf> shelf)
        {
            context.Changes().RecordCall([placing = std::move(shelf)](App& app) {
                placing->placed = true;
                Scene& scene = app.GetActiveScene();
                if (&scene == placing->scene)
                {
                    scene.AddResource<Detail::RagdollDescriptions>(Detail::RagdollDescriptions{placing});
                }
            });
        }
    };
} // namespace Fenrir
