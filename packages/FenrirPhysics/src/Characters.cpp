// A physics world's character bodies (ADR-0014): made and destroyed with the bodies' batch, moved in PostTick step 3b
// and written back in step 5. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstdint>
#include <set>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "Same.hpp"
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
                m_characters.emplace(entities[i],
                                     CharacterRecord{.id = ids[i],
                                                     .moved = Backend::CharacterMoved{},
                                                     .world = GetWorldTransform(m_entities->GetEntity(entities[i])),
                                                     .shape = descs[i].shape,
                                                     .scale = descs[i].scale});
            }
        }
    }

    void PhysicsWorld::Impl::PushCharacterEdits()
    {
        for (auto& [id, record] : m_characters)
        {
            const Transform world = GetWorldTransform(m_entities->GetEntity(id));
            if (!Same(world.pos, record.world.pos))
            {
                // Its contacts and ground are found again where it now is, so this tick's move starts from there.
                m_backend.TeleportCharacter(record.id, world.pos, world.rot);
                record.moved.carriedVelocity = Math::Vec3(0.0f);
                record.shown.snap = true;
            }
            else if (!Same(world.rot, record.world.rot))
            {
                // Not a teleport: the next move pushes the turned shape out of anything it now overlaps.
                m_backend.TurnCharacter(record.id, world.rot);
            }
            record.world = world;
        }
    }

    void PhysicsWorld::Impl::MoveCharacters(float dt)
    {
        for (auto& [id, record] : m_characters)
        {
            const Entity entity = m_entities->GetEntity(id);
            ApplyShape(entity, record);
            const CharacterBody& body = entity.GetComponent<CharacterBody>();
            record.moved = m_backend.MoveCharacter(record.id,
                                                   Backend::CharacterMove{.velocity = body.velocity,
                                                                          .carried = record.moved.carriedVelocity,
                                                                          .maxSlope = body.maxSlope,
                                                                          .stepHeight = body.stepHeight,
                                                                          .floorSnap = body.floorSnap,
                                                                          .stopOnSlope = body.stopOnSlope},
                                                   dt);
        }
    }

    void PhysicsWorld::Impl::ApplyShape(const Entity& entity, CharacterRecord& record)
    {
        const CollisionShape& shape = entity.GetComponent<Collider>().shape;
        const Math::Vec3 scale = GetWorldTransform(entity).scale;
        record.shapeBlocked = false;
        if ((Same(shape, record.shape) && Same(scale, record.scale)) || shape.Is<MeshShape>() ||
            !m_backend.CanMakeShape(shape))
        {
            return;
        }
        // Crouching on the ground lowers the head; in the air it tucks the feet up.
        const bool onGround =
            record.moved.ground == GroundState::OnGround || record.moved.ground == GroundState::OnSteepGround;
        if (!m_backend.ReshapeCharacter(record.id, shape, scale, onGround))
        {
            // The Collider keeps what game code asked for, and the next move tries it again.
            record.shapeBlocked = true;
            return;
        }
        record.shape = shape;
        record.scale = scale;
    }

    void PhysicsWorld::Impl::WriteBackCharacter(Entity& entity, CharacterRecord& record)
    {
        // Only the position: the rotation and scale stay game code's, bit for bit, not recomputed through a parent.
        const Backend::CharacterMoved& moved = record.moved;
        Transform world = GetWorldTransform(entity);
        world.pos = moved.position;
        Transform& local = entity.GetComponent<Transform>();
        const Transform kept = local;
        SetWorldTransform(entity, world);
        local.rot = kept.rot;
        local.scale = kept.scale;
        // Read back the way the diff reads, so rounding through a parent is not taken for a teleport.
        record.world = GetWorldTransform(entity);

        const auto handle = [this](std::uint32_t id) { return id == ~0u ? Entity() : m_entities->HandleOf(id); };
        CharacterBody& body = entity.GetComponent<CharacterBody>();
        body.velocity = moved.velocity;
        body.state = CharacterState{.ground = moved.ground,
                                    .groundBody = handle(moved.groundEntity),
                                    .groundCollider = handle(moved.groundCollider),
                                    .groundPoint = moved.groundPoint,
                                    .groundNormal = moved.groundNormal,
                                    .groundVelocity = moved.groundVelocity,
                                    .groundAngularVelocity = moved.groundAngularVelocity,
                                    .movedVelocity = moved.movedVelocity,
                                    .carriedVelocity = moved.carriedVelocity,
                                    .onWall = moved.onWall,
                                    .onCeiling = moved.onCeiling,
                                    .shapeBlocked = record.shapeBlocked};
    }
} // namespace Fenrir
