// The physics world's queries: the backend finds the bodies, this file names their entities and fills the caller's
// buffers.

#include <algorithm>
#include <array>
#include <cstdint>

#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // The gap a motion test leaves between a shape and what it was moved against, along that body's normal: the
        // character's padding, so a custom mover and a character keep the same distance from walls.
        constexpr float skin = 0.02f;

        // How many times Depenetrate looks again from where its last offset put the shape, and how many overlaps it
        // takes into account each time: a few, as a shape is rarely wedged between more.
        constexpr int depenetrateTries = 4;
        constexpr std::size_t depenetrateOverlaps = 16;

        // The unit direction of `direction`, or false if it has none.
        bool UnitDirection(const Math::Vec3& direction, Math::Vec3& unit)
        {
            const float length = glm::length(direction);
            if (!(length > 0.0f))
            {
                return false;
            }
            unit = direction / length;
            return true;
        }

        // The entity a body was made for, or false if it has been destroyed since the last step.
        bool EntityOf(EntityList& entities, std::uint32_t id, Entity& entity)
        {
            if (!entities.HasEntity(id))
            {
                return false;
            }
            entity = entities.GetEntity(id);
            return true;
        }

        // Keeps the nearest of the hits it is given in `hits`, nearest first.
        template <typename Hit>
        class Nearest
        {
          public:
            Nearest(EntityList& entities, std::span<Hit> hits, float maxDistance)
                : m_entities(&entities), m_hits(hits), m_maxDistance(maxDistance)
            {
            }

            float operator()(const Backend::QueryHit& found)
            {
                if (m_count == m_hits.size() && found.distance >= m_hits[m_count - 1].distance)
                {
                    return Farthest();
                }
                Entity body;
                Entity collider;
                if (!EntityOf(*m_entities, found.entity, body) || !EntityOf(*m_entities, found.collider, collider))
                {
                    return Farthest();
                }
                // After any hits at the same distance, so ties keep the order they were found in.
                const auto at = std::upper_bound(
                    m_hits.begin(), m_hits.begin() + static_cast<std::ptrdiff_t>(m_count), found.distance,
                    [](float distance, const Hit& hit) { return distance < hit.distance; });
                if (m_count < m_hits.size())
                {
                    ++m_count;
                }
                std::move_backward(at, m_hits.begin() + static_cast<std::ptrdiff_t>(m_count) - 1,
                                   m_hits.begin() + static_cast<std::ptrdiff_t>(m_count));
                *at = Hit{body, collider, found.point, found.normal, found.distance};
                return Farthest();
            }

            [[nodiscard]] std::size_t Count() const
            {
                return m_count;
            }

          private:
            float Farthest() const
            {
                return m_count == m_hits.size() ? m_hits[m_count - 1].distance : m_maxDistance;
            }

            EntityList* m_entities;
            std::span<Hit> m_hits;
            float m_maxDistance;
            std::size_t m_count = 0;
        };

        // Keeps the deepest of the penetrations it is given in `hits`, deepest first, each collider once at its
        // deepest. Ties go to the lower collider entity, since the backend gives them in no particular order.
        class Deepest
        {
          public:
            Deepest(EntityList& entities, std::span<PenetrationHit> hits) : m_entities(&entities), m_hits(hits)
            {
            }

            void operator()(const Backend::Penetration& found)
            {
                PenetrationHit hit{Entity(), Entity(), found.point, found.normal, found.depth};
                if (!EntityOf(*m_entities, found.entity, hit.body) ||
                    !EntityOf(*m_entities, found.collider, hit.collider))
                {
                    return;
                }
                const std::span<PenetrationHit> before = Listed();
                if (const auto same = std::ranges::find(before, hit.collider, &PenetrationHit::collider);
                    same != before.end())
                {
                    if (!Deeper(hit, *same))
                    {
                        return;
                    }
                    std::move(same + 1, before.end(), same);
                    --m_count;
                }
                if (m_count == m_hits.size() && !Deeper(hit, m_hits[m_count - 1]))
                {
                    return;
                }
                // The last place is free, or holds the shallowest, which the new one pushes out.
                if (m_count < m_hits.size())
                {
                    ++m_count;
                }
                const std::span<PenetrationHit> listed = Listed();
                const auto at = std::upper_bound(listed.begin(), listed.end() - 1, hit, Deeper);
                std::move_backward(at, listed.end() - 1, listed.end());
                *at = hit;
            }

            [[nodiscard]] std::size_t Count() const
            {
                return m_count;
            }

          private:
            static bool Deeper(const PenetrationHit& a, const PenetrationHit& b)
            {
                return a.depth > b.depth || (!(a.depth < b.depth) && a.collider.GetId() < b.collider.GetId());
            }

            std::span<PenetrationHit> Listed() const
            {
                return m_hits.first(m_count);
            }

            EntityList* m_entities;
            std::span<PenetrationHit> m_hits;
            std::size_t m_count = 0;
        };

        // Writes the colliders it is given, with their bodies, to `hits` until it is full.
        class Overlaps
        {
          public:
            Overlaps(EntityList& entities, std::span<OverlapHit> hits) : m_entities(&entities), m_hits(hits)
            {
            }

            bool operator()(std::uint32_t bodyId, std::uint32_t colliderId)
            {
                Entity body;
                Entity collider;
                if (EntityOf(*m_entities, bodyId, body) && EntityOf(*m_entities, colliderId, collider))
                {
                    m_hits[m_count++] = OverlapHit{body, collider};
                }
                return m_count < m_hits.size();
            }

            [[nodiscard]] std::size_t Count() const
            {
                return m_count;
            }

          private:
            EntityList* m_entities;
            std::span<OverlapHit> m_hits;
            std::size_t m_count = 0;
        };
    } // namespace

    Backend::QueryFilter PhysicsWorld::Impl::ToBackend(const QueryFilter& filter) const
    {
        const std::uint32_t ignored =
            filter.ignoreBody.IsValid() ? BodyView().ResolveBody(filter.ignoreBody.GetId()) : ~0u;
        return Backend::QueryFilter{filter.mask, filter.layers, ignored, filter.hitTriggers};
    }

    std::optional<RaycastHit> PhysicsWorld::Raycast(const Math::Vec3& origin, const Math::Vec3& direction,
                                                    float maxDistance, const QueryFilter& filter) const
    {
        RaycastHit hit;
        if (RaycastAll(origin, direction, maxDistance, std::span(&hit, 1), filter) == 0)
        {
            return std::nullopt;
        }
        return hit;
    }

    std::size_t PhysicsWorld::RaycastAll(const Math::Vec3& origin, const Math::Vec3& direction, float maxDistance,
                                         std::span<RaycastHit> hits, const QueryFilter& filter) const
    {
        Math::Vec3 unit;
        if (hits.empty() || !UnitDirection(direction, unit))
        {
            return 0;
        }
        Nearest<RaycastHit> nearest(*m_impl->m_entities, hits, maxDistance);
        m_impl->m_backend.CastRay(origin, unit, maxDistance, m_impl->ToBackend(filter), nearest);
        return nearest.Count();
    }

    std::optional<ShapeCastHit> PhysicsWorld::ShapeCast(const CollisionShape& shape, const Transform& pose,
                                                        const Math::Vec3& direction, float maxDistance,
                                                        const QueryFilter& filter) const
    {
        ShapeCastHit hit;
        if (ShapeCastAll(shape, pose, direction, maxDistance, std::span(&hit, 1), filter) == 0)
        {
            return std::nullopt;
        }
        return hit;
    }

    std::size_t PhysicsWorld::ShapeCastAll(const CollisionShape& shape, const Transform& pose,
                                           const Math::Vec3& direction, float maxDistance, std::span<ShapeCastHit> hits,
                                           const QueryFilter& filter) const
    {
        Math::Vec3 unit;
        if (hits.empty() || !UnitDirection(direction, unit))
        {
            return 0;
        }
        Nearest<ShapeCastHit> nearest(*m_impl->m_entities, hits, maxDistance);
        m_impl->m_backend.CastShape(shape, pose.scale, pose.pos, pose.rot, unit, maxDistance, m_impl->ToBackend(filter),
                                    nearest);
        return nearest.Count();
    }

    std::optional<MoveCastHit> PhysicsWorld::MoveCast(const CollisionShape& shape, const Transform& pose,
                                                      const Math::Vec3& motion, const QueryFilter& filter) const
    {
        Math::Vec3 unit;
        if (shape.Is<MeshShape>() || !UnitDirection(motion, unit))
        {
            return std::nullopt;
        }
        // Swept on by the skin, so a body just past the end of the motion still keeps the shape the skin away.
        const float length = glm::length(motion);
        MoveCastHit hit;
        Nearest<MoveCastHit> nearest(*m_impl->m_entities, std::span(&hit, 1), length + skin);
        m_impl->m_backend.CastMove(shape, pose.scale, pose.pos, pose.rot, unit, length + skin,
                                   m_impl->ToBackend(filter), nearest);
        if (nearest.Count() == 0)
        {
            return std::nullopt;
        }
        // Back from where it touches until the gap along the body's normal is the skin. Only bodies the shape moves
        // towards block it, so the normal is against the motion.
        const float towards = -glm::dot(hit.normal, unit);
        hit.distance = std::max(0.0f, hit.distance - skin / towards);
        return hit;
    }

    std::size_t PhysicsWorld::Overlap(const CollisionShape& shape, const Transform& pose, std::span<OverlapHit> hits,
                                      const QueryFilter& filter) const
    {
        if (hits.empty())
        {
            return 0;
        }
        Overlaps overlaps(*m_impl->m_entities, hits);
        m_impl->m_backend.CollideShape(shape, pose.scale, pose.pos, pose.rot, m_impl->ToBackend(filter), overlaps);
        return overlaps.Count();
    }

    std::size_t PhysicsWorld::Penetrations(const CollisionShape& shape, const Transform& pose,
                                           std::span<PenetrationHit> hits, const QueryFilter& filter) const
    {
        if (hits.empty() || shape.Is<MeshShape>())
        {
            return 0;
        }
        Deepest deepest(*m_impl->m_entities, hits);
        m_impl->m_backend.CollideDeep(shape, pose.scale, pose.pos, pose.rot, m_impl->ToBackend(filter), deepest);
        return deepest.Count();
    }

    Math::Vec3 PhysicsWorld::Depenetrate(const CollisionShape& shape, const Transform& pose,
                                         const QueryFilter& filter) const
    {
        std::array<PenetrationHit, depenetrateOverlaps> hits;
        Transform moved = pose;
        for (int i = 0; i < depenetrateTries; ++i)
        {
            const std::size_t count = Penetrations(shape, moved, hits, filter);
            if (count == 0)
            {
                break;
            }
            // Deepest first, each overlap only by what the ones before it have not already moved the shape along its
            // normal: two walls at right angles add up, two facing the same way do not.
            Math::Vec3 step(0.0f);
            for (const PenetrationHit& hit : std::span(hits).first(count))
            {
                const float remaining = hit.depth + skin - glm::dot(step, hit.normal);
                if (remaining > 0.0f)
                {
                    step += hit.normal * remaining;
                }
            }
            moved.pos += step;
        }
        return moved.pos - pose.pos;
    }

    std::size_t PhysicsWorld::OverlapPoint(const Math::Vec3& point, std::span<OverlapHit> hits,
                                           const QueryFilter& filter) const
    {
        if (hits.empty())
        {
            return 0;
        }
        Overlaps overlaps(*m_impl->m_entities, hits);
        m_impl->m_backend.CollidePoint(point, m_impl->ToBackend(filter), overlaps);
        return overlaps.Count();
    }

    bool PhysicsWorld::IsSleeping(const Entity& entity) const
    {
        const Impl::Record* record = m_impl->BodyView().FindBody(entity);
        return record != nullptr && record->Active() && m_impl->m_backend.IsSleeping(record->id);
    }

    std::optional<Math::AABB> PhysicsWorld::Bounds(const Entity& entity) const
    {
        const Impl::Record* record = m_impl->BodyView().FindBody(entity);
        if (record == nullptr || !record->Active())
        {
            return std::nullopt; // no body, or an inactive one
        }
        return m_impl->m_backend.Bounds(record->id);
    }
} // namespace Fenrir
