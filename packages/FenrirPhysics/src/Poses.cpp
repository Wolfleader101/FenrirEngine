#include "Poses.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "Hierarchy.hpp"
#include "Same.hpp"

namespace Fenrir::Detail
{
    void Poses::Shown::Advance(const Transform& world)
    {
        previous = snap ? world : current;
        current = world;
        snap = false;
        heldByGame = false;
    }

    void Poses::Shown::Restore(Transform& local, bool turnIsTeleport)
    {
        const bool moved = !Same(local.pos, shownLocal.pos);
        const bool turned = !Same(local.rot, shownLocal.rot) || !Same(local.scale, shownLocal.scale);
        // Game code moved or turned it during Update: what it changed stays, and the rest goes back to the true pose,
        // so a turn alone does not leave it at its blended position.
        if (!moved)
        {
            local.pos = trueLocal.pos;
        }
        if (!turned)
        {
            local.rot = trueLocal.rot;
            local.scale = trueLocal.scale;
        }
        if (moved || turned)
        {
            heldByGame = true;
            // Nothing blends across a jump.
            snap = snap || moved || turnIsTeleport;
        }
    }

    Transform Poses::Shown::Between(float fraction) const
    {
        Transform world = current;
        world.pos = previous.pos + (current.pos - previous.pos) * fraction;
        world.rot = Math::Slerp(previous.rot, current.rot, fraction);
        return world;
    }

    Poses::Poses(EntityList& entities) : m_entities(&entities)
    {
    }

    void Poses::HandBody(std::uint32_t entity, const Transform& world, BodyPose pose)
    {
        Entry& entry = m_bodies[entity];
        entry.step = world;
        entry.pose = pose;
        if (pose == BodyPose::Written)
        {
            m_pending.push_back(Pending{DepthOf(m_entities->GetEntity(entity)), entity, false, world});
        }
    }

    void Poses::HandCharacter(std::uint32_t entity, const Math::Vec3& position)
    {
        Transform world;
        world.pos = position;
        m_pending.push_back(Pending{DepthOf(m_entities->GetEntity(entity)), entity, true, world});
    }

    void Poses::WriteBack()
    {
        // Parents before children: a body's or character's local Transform is written against its parent's world
        // transform, so a parent written after it would move it again.
        std::ranges::sort(m_pending, [](const Pending& a, const Pending& b) {
            return std::tie(a.depth, a.entity, a.isCharacter) < std::tie(b.depth, b.entity, b.isCharacter);
        });
        for (const Pending& pending : m_pending)
        {
            Entity entity = m_entities->GetEntity(pending.entity);
            if (!pending.isCharacter)
            {
                SetWorldTransform(entity, pending.world);
                m_bodies[pending.entity].step = GetWorldTransform(entity);
                continue;
            }
            // Only the position: the rotation and scale stay game code's, bit for bit, not recomputed through a parent.
            Transform world = GetWorldTransform(entity);
            world.pos = pending.world.pos;
            Transform& local = entity.GetComponent<Transform>();
            const Transform kept = local;
            SetWorldTransform(entity, world);
            local.rot = kept.rot;
            local.scale = kept.scale;
            m_characters[pending.entity].step = GetWorldTransform(entity);
        }
        m_pending.clear();
    }

    void Poses::SnapBody(std::uint32_t entity)
    {
        m_bodies[entity].shown.snap = true;
    }

    void Poses::SnapCharacter(std::uint32_t entity)
    {
        m_characters[entity].shown.snap = true;
    }

    void Poses::ForgetBody(std::uint32_t entity)
    {
        m_bodies.erase(entity);
    }

    void Poses::ForgetCharacter(std::uint32_t entity)
    {
        m_characters.erase(entity);
    }

    const Transform& Poses::BodyStepPose(std::uint32_t entity) const
    {
        return m_bodies.at(entity).step;
    }

    const Transform& Poses::CharacterStepPose(std::uint32_t entity) const
    {
        return m_characters.at(entity).step;
    }

    const Poses::Shown& Poses::BodyShown(std::uint32_t entity) const
    {
        static const Shown fresh{};
        const auto it = m_bodies.find(entity);
        return it == m_bodies.end() ? fresh : it->second.shown;
    }

    void Poses::Advance()
    {
        for (auto& [id, entry] : m_bodies)
        {
            entry.shown.Advance(entry.step);
        }
        for (auto& [id, entry] : m_characters)
        {
            entry.shown.Advance(entry.step);
        }
    }

    void Poses::Restore()
    {
        const auto restore = [this](std::uint32_t id, Shown& shown, bool turnIsTeleport) {
            if (shown.blended && m_entities->HasEntity(id))
            {
                shown.Restore(m_entities->GetEntity(id).GetComponent<Transform>(), turnIsTeleport);
            }
            shown.blended = false;
        };
        for (auto& [id, entry] : m_bodies)
        {
            restore(id, entry.shown, true);
        }
        // A character's turn is not a teleport.
        for (auto& [id, entry] : m_characters)
        {
            restore(id, entry.shown, false);
        }
    }

    void Poses::Blend(float fraction)
    {
        struct Blended
        {
            int depth;
            Entity entity;
            const Transform* stepPose;
            Shown* shown;
            // Shown at its step pose: one that is not interpolated, written only because an ancestor is.
            bool atStepPose;
        };
        std::vector<Blended> blended;
        std::vector<Blended> carried;
        std::set<std::uint32_t> interpolated;
        const auto add = [&](std::uint32_t id, const Transform& stepPose, Shown& shown, bool interpolate) {
            Entity entity = m_entities->GetEntity(id);
            if (interpolate)
            {
                interpolated.insert(id);
                blended.push_back(Blended{DepthOf(entity), entity, &stepPose, &shown, false});
            }
            else
            {
                carried.push_back(Blended{DepthOf(entity), entity, &stepPose, &shown, true});
            }
        };
        for (auto& [id, entry] : m_bodies)
        {
            if (entry.pose == BodyPose::Still || entry.shown.heldByGame || !m_entities->HasEntity(id))
            {
                continue; // an inactive collider has no simulated pose to show
            }
            const Entity entity = m_entities->GetEntity(id);
            if (!entity.HasComponent<RigidBody>() || !entity.HasComponent<Transform>())
            {
                continue;
            }
            add(id, entry.step, entry.shown, entity.GetComponent<RigidBody>().interpolate);
        }
        for (auto& [id, entry] : m_characters)
        {
            if (entry.shown.heldByGame || !m_entities->HasEntity(id))
            {
                continue;
            }
            add(id, entry.step, entry.shown, m_entities->GetEntity(id).GetComponent<CharacterBody>().interpolate);
        }
        // One that is not interpolated is still moved by an interpolated ancestor's blend, through its local
        // Transform: it is written back at its own step pose, so it is shown where it is.
        for (const Blended& body : carried)
        {
            for (Entity above = ParentOf(body.entity); above.IsValid(); above = ParentOf(above))
            {
                if (interpolated.contains(above.GetId()))
                {
                    blended.push_back(body);
                    break;
                }
            }
        }
        // Parents before children, so each child's world pose is set against its parent's blended one.
        std::stable_sort(blended.begin(), blended.end(),
                         [](const Blended& a, const Blended& b) { return a.depth < b.depth; });

        // Every true local Transform is saved before any is overwritten.
        for (const Blended& body : blended)
        {
            body.shown->trueLocal = body.entity.GetComponent<Transform>();
        }
        for (const Blended& body : blended)
        {
            Shown& shown = *body.shown;
            Transform world = *body.stepPose;
            if (!body.atStepPose)
            {
                const Transform between = shown.Between(fraction);
                world.pos = between.pos;
                world.rot = between.rot;
            }
            SetWorldTransform(body.entity, world);
            shown.shownLocal = body.entity.GetComponent<Transform>();
            shown.blended = true;
        }
    }
} // namespace Fenrir::Detail
