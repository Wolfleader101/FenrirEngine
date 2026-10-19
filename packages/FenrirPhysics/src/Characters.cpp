// A physics world's character bodies (ADR-0014): made and destroyed with the bodies' batch, moved in PostTick step 3b
// and written back in step 5. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstdint>
#include <set>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // Whether the character body on `entity` can have a backend character now.
        bool CanBeCharacter(const Entity& entity, Backend::World& backend)
        {
            if (entity.HasComponent<RigidBody>() || !entity.HasComponent<Collider>())
            {
                return false;
            }
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;
            return !shape.Is<MeshShape>() && backend.CanMakeShape(shape);
        }
    } // namespace

    bool PhysicsWorld::Impl::IsCharacterCollider(std::uint32_t entity) const
    {
        const Entity collider = m_entities->GetEntity(entity);
        // Beside a RigidBody the RigidBody works instead, and the Collider is its.
        return collider.HasComponent<CharacterBody>() && !collider.HasComponent<RigidBody>();
    }

    void PhysicsWorld::Impl::DropCharacters(const std::set<std::uint32_t>& changed)
    {
        std::vector<Backend::CharacterId> destroyed;
        for (auto it = m_characters.begin(); it != m_characters.end();)
        {
            if (!changed.contains(it->first))
            {
                ++it;
                continue;
            }
            destroyed.push_back(it->second.id);
            it = m_characters.erase(it);
        }
        m_backend.DestroyCharacters(destroyed);
    }

    void PhysicsWorld::Impl::MakeCharacters()
    {
        std::vector<std::uint32_t> entities;
        std::vector<Backend::CharacterDesc> descs;
        for (std::uint32_t id : m_characterEntities)
        {
            const Entity entity = m_entities->GetEntity(id);
            if (m_characters.contains(id) || !CanBeCharacter(entity, m_backend))
            {
                continue;
            }
            const Transform world = GetWorldTransform(entity);
            const CollisionFilter filter =
                entity.HasComponent<CollisionFilter>() ? entity.GetComponent<CollisionFilter>() : CollisionFilter{};
            entities.push_back(id);
            descs.push_back(Backend::CharacterDesc{.shape = entity.GetComponent<Collider>().shape,
                                                   .scale = world.scale,
                                                   .filter = filter,
                                                   .position = world.pos,
                                                   .rotation = world.rot,
                                                   .entity = id});
        }
        std::vector<Backend::CharacterId> ids(descs.size());
        m_backend.CreateCharacters(descs, ids);
        for (std::size_t i = 0; i < descs.size(); ++i)
        {
            if (ids[i].value != Backend::CharacterId{}.value)
            {
                m_characters.emplace(entities[i], CharacterRecord{ids[i], Backend::CharacterMoved{}});
            }
        }
    }

    void PhysicsWorld::Impl::MoveCharacters(float dt)
    {
        for (auto& [id, record] : m_characters)
        {
            const CharacterBody& body = m_entities->GetEntity(id).GetComponent<CharacterBody>();
            record.moved = m_backend.MoveCharacter(record.id,
                                                   Backend::CharacterMove{.velocity = body.velocity,
                                                                          .maxSlope = body.maxSlope,
                                                                          .stepHeight = body.stepHeight,
                                                                          .floorSnap = body.floorSnap,
                                                                          .stopOnSlope = body.stopOnSlope},
                                                   dt);
        }
    }

    void PhysicsWorld::Impl::WriteBackCharacter(Entity& entity, const Backend::CharacterMoved& moved)
    {
        // Only the position: the rotation and scale stay game code's.
        Transform world = GetWorldTransform(entity);
        world.pos = moved.position;
        SetWorldTransform(entity, world);

        const auto handle = [this](std::uint32_t id) { return id == ~0u ? Entity() : m_entities->HandleOf(id); };
        CharacterBody& body = entity.GetComponent<CharacterBody>();
        body.velocity = moved.velocity;
        body.state = CharacterState{.ground = moved.ground,
                                    .groundBody = handle(moved.groundEntity),
                                    .groundCollider = handle(moved.groundCollider),
                                    .groundPoint = moved.groundPoint,
                                    .groundNormal = moved.groundNormal,
                                    .movedVelocity = moved.movedVelocity,
                                    .onWall = moved.onWall,
                                    .onCeiling = moved.onCeiling};
    }
} // namespace Fenrir
