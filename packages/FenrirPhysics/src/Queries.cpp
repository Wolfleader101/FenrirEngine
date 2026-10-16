// The physics world's queries: the backend finds the bodies, this file names their entities and fills the caller's
// buffers.

#include <algorithm>
#include <cstdint>

#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        Backend::QueryFilter ToBackend(const QueryFilter& filter)
        {
            return Backend::QueryFilter{filter.mask, filter.ignoreBody.GetId(), filter.hitTriggers};
        }

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
        m_impl->m_backend.CastRay(origin, unit, maxDistance, ToBackend(filter), nearest);
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
        m_impl->m_backend.CastShape(shape, pose.scale, pose.pos, pose.rot, unit, maxDistance, ToBackend(filter),
                                    nearest);
        return nearest.Count();
    }

    std::size_t PhysicsWorld::Overlap(const CollisionShape& shape, const Transform& pose, std::span<OverlapHit> hits,
                                      const QueryFilter& filter) const
    {
        if (hits.empty())
        {
            return 0;
        }
        Overlaps overlaps(*m_impl->m_entities, hits);
        m_impl->m_backend.CollideShape(shape, pose.scale, pose.pos, pose.rot, ToBackend(filter), overlaps);
        return overlaps.Count();
    }

    std::size_t PhysicsWorld::OverlapPoint(const Math::Vec3& point, std::span<OverlapHit> hits,
                                           const QueryFilter& filter) const
    {
        if (hits.empty())
        {
            return 0;
        }
        Overlaps overlaps(*m_impl->m_entities, hits);
        m_impl->m_backend.CollidePoint(point, ToBackend(filter), overlaps);
        return overlaps.Count();
    }

    bool PhysicsWorld::IsSleeping(const Entity& body) const
    {
        const Impl::Record* record = m_impl->FindBody(body.GetId());
        return record != nullptr && record->Active() && m_impl->m_backend.IsSleeping(record->id);
    }

    std::optional<Math::AABB> PhysicsWorld::Bounds(const Entity& body) const
    {
        const Impl::Record* record = m_impl->FindBody(body.GetId());
        if (record == nullptr || !record->Active())
        {
            return std::nullopt; // no body, or an inactive one
        }
        return m_impl->m_backend.Bounds(record->id);
    }
} // namespace Fenrir
