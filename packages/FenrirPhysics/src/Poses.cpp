#include "Poses.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "Hierarchy.hpp"
#include "Same.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // Writes a wheel's world position and rotation; its scale stays game code's.
        void PoseWheel(Entity wheel, const Math::Vec3& pos, const Math::Quat& rot)
        {
            Transform world = GetWorldTransform(wheel);
            world.pos = pos;
            world.rot = rot;
            SetWorldTransform(wheel, world);
        }

        // A vehicle's wheels, parents before children: a wheel's local Transform is written against its parent's world
        // transform.
        template <typename Wheels>
        void SortWheels(Wheels& wheels)
        {
            std::ranges::sort(wheels, [](const auto& a, const auto& b) {
                return std::tie(a.depth, a.entity) < std::tie(b.depth, b.entity);
            });
        }
    } // namespace

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

    void Poses::Made(std::uint32_t entity, const Transform& world)
    {
        m_entries[entity] = Entry{.kind = Kind::Still, .baseline = world, .shown = Shown{}};
    }

    void Poses::Forget(std::uint32_t entity)
    {
        m_entries.erase(entity);
    }

    Poses::Edit Poses::Push(std::uint32_t entity, Kind kind, const Transform& world)
    {
        Entry& entry = m_entries.at(entity);
        Edit edit = Edit::None;
        if (!Same(world.pos, entry.baseline.pos))
        {
            edit = Edit::Moved;
        }
        else if (!Same(world.rot, entry.baseline.rot))
        {
            edit = Edit::Turned;
        }
        entry.kind = kind;
        entry.baseline = world;
        // Nothing blends across a jump. A character's turn is not one, and a followed body is moved by the step, not
        // teleported.
        if (kind != Kind::Followed && (edit == Edit::Moved || (edit == Edit::Turned && kind != Kind::Character)))
        {
            entry.shown.snap = true;
        }
        return edit;
    }

    void Poses::Hand(std::uint32_t entity, Kind kind, const Transform& world)
    {
        Entry& entry = m_entries.at(entity);
        entry.kind = kind;
        if (kind == Kind::Followed || kind == Kind::Still)
        {
            entry.baseline = world;
            return;
        }
        m_pending.push_back(Pending{DepthOf(m_entities->GetEntity(entity)), entity, kind == Kind::Character, world});
    }

    void Poses::HandWheel(std::uint32_t wheel, std::uint32_t chassis, const Transform& pose)
    {
        m_pendingWheels[chassis].push_back(HandedWheel{DepthOf(m_entities->GetEntity(wheel)), wheel, pose});
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
            Entry& entry = m_entries.at(pending.entity);
            if (!pending.isCharacter)
            {
                SetWorldTransform(entity, pending.world);
                entry.baseline = GetWorldTransform(entity);
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
            entry.baseline = GetWorldTransform(entity);
        }
        m_pending.clear();

        // After the bodies: a wheel is written against its chassis's step pose, read back above.
        for (auto& [chassis, wheels] : m_pendingWheels)
        {
            const Transform& at = m_entries.at(chassis).baseline;
            SortWheels(wheels);
            for (const HandedWheel& wheel : wheels)
            {
                PoseWheel(m_entities->GetEntity(wheel.entity), at.pos + at.rot * wheel.pose.pos,
                          at.rot * wheel.pose.rot);
            }
        }
        m_pendingWheels.clear();
    }

    const Transform& Poses::Baseline(std::uint32_t entity) const
    {
        return m_entries.at(entity).baseline;
    }

    const Transform& Poses::StepPose(std::uint32_t entity) const
    {
        static const Transform none{};
        const auto it = m_entries.find(entity);
        return it == m_entries.end() ? none : it->second.shown.current;
    }

    bool Poses::Snaps(std::uint32_t entity) const
    {
        const auto it = m_entries.find(entity);
        return it == m_entries.end() || it->second.shown.snap;
    }

    std::optional<Transform> Poses::BlendedPose(std::uint32_t entity, float fraction) const
    {
        const auto it = m_entries.find(entity);
        if (it == m_entries.end() || !Interpolated(it->first, it->second).value_or(false))
        {
            return std::nullopt;
        }
        return it->second.shown.Between(fraction);
    }

    void Poses::HandBlendedWheel(std::uint32_t wheel, std::uint32_t chassis, const Transform& world)
    {
        m_blendingWheels[chassis].push_back(HandedWheel{DepthOf(m_entities->GetEntity(wheel)), wheel, world});
    }

    void Poses::Advance()
    {
        for (auto& [id, entry] : m_entries)
        {
            entry.shown.Advance(entry.baseline);
        }
    }

    void Poses::Restore()
    {
        for (auto& [id, entry] : m_entries)
        {
            Shown& shown = entry.shown;
            if (shown.blended && m_entities->HasEntity(id))
            {
                // A character's turn is not a teleport.
                shown.Restore(m_entities->GetEntity(id).GetComponent<Transform>(), entry.kind != Kind::Character);
            }
            shown.blended = false;
        }
        // A wheel's Transform is output only: whatever game code wrote since goes, as the next step would overwrite it.
        // A wheel entity destroyed since is skipped, though its id may name a newer entity.
        for (const auto& [chassis, wheels] : m_blendedWheels)
        {
            for (const auto& [wheel, local] : wheels)
            {
                if (wheel.IsValid())
                {
                    wheel.GetComponent<Transform>() = local;
                }
            }
        }
        m_blendedWheels.clear();
    }

    std::optional<bool> Poses::Interpolated(std::uint32_t id, const Entry& entry) const
    {
        if (entry.kind == Kind::Still || entry.shown.heldByGame || !m_entities->HasEntity(id))
        {
            return std::nullopt; // an inactive collider has no simulated pose to show
        }
        const Entity entity = m_entities->GetEntity(id);
        if (entry.kind == Kind::Character)
        {
            return entity.GetComponent<CharacterBody>().interpolate;
        }
        if (!entity.HasComponent<RigidBody>() || !entity.HasComponent<Transform>())
        {
            return std::nullopt;
        }
        return entity.GetComponent<RigidBody>().interpolate;
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
        for (auto& [id, entry] : m_entries)
        {
            const std::optional<bool> interpolate = Interpolated(id, entry);
            if (!interpolate)
            {
                continue;
            }
            const Entity entity = m_entities->GetEntity(id);
            if (*interpolate)
            {
                interpolated.insert(id);
                blended.push_back(Blended{DepthOf(entity), entity, &entry.baseline, &entry.shown, false});
            }
            else
            {
                carried.push_back(Blended{DepthOf(entity), entity, &entry.baseline, &entry.shown, true});
            }
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

        // After the bodies: each wheel is posed on its chassis's blended pose. Every true local Transform of a
        // vehicle's wheels is saved before any is overwritten.
        for (auto& [chassis, wheels] : m_blendingWheels)
        {
            SortWheels(wheels);
            std::vector<std::pair<Entity, Transform>>& trueLocals = m_blendedWheels[chassis];
            trueLocals.clear();
            for (const HandedWheel& wheel : wheels)
            {
                const Entity entity = m_entities->GetEntity(wheel.entity);
                trueLocals.emplace_back(entity, entity.GetComponent<Transform>());
            }
            for (const HandedWheel& wheel : wheels)
            {
                PoseWheel(m_entities->GetEntity(wheel.entity), wheel.pose.pos, wheel.pose.rot);
            }
        }
        m_blendingWheels.clear();
    }
} // namespace Fenrir::Detail
