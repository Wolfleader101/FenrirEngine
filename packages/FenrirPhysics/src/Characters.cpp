// A physics world's character bodies (ADR-0014): made and destroyed with the bodies' batch, moved in PostTick's moves
// stage and written back in its write-back. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FilterOf.hpp"
#include "Same.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // Why the character body on `entity` cannot have a backend character now (it is inactive), or empty if it can.
        std::string WhyNoCharacter(const Entity& entity, Backend::World& backend)
        {
            if (entity.HasComponent<RigidBody>())
            {
                return "its entity has a RigidBody, which works instead";
            }
            if (!entity.HasComponent<Collider>())
            {
                return "its entity has no Collider to give it a shape";
            }
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;
            if (shape.Is<MeshShape>())
            {
                return "its Collider is a MeshShape, which a character cannot use";
            }
            if (!backend.CanMakeShape(shape))
            {
                return "its Collider refers to a collision mesh that is released or cannot be used as its "
                       "kind of shape";
            }
            return {};
        }

        // A character's body is its entity's: the entity's PhysicsMaterial is what other bodies feel on it, and its
        // CollisionFilter filters both the body and the character's moves.
        PhysicsMaterial MaterialOf(const Entity& entity)
        {
            return entity.HasComponent<PhysicsMaterial>() ? entity.GetComponent<PhysicsMaterial>() : PhysicsMaterial{};
        }

        // Whether the character body on `entity` should have a backend character now: enabled, and not inactive.
        bool ShouldBeCharacter(const Entity& entity, Backend::World& backend)
        {
            return entity.GetComponent<CharacterBody>().enabled && WhyNoCharacter(entity, backend).empty();
        }
    } // namespace

    void PhysicsWorld::Impl::DropCharacters(const std::set<std::uint32_t>& changed)
    {
        // What came or went is in `changed`, from Bodies' own CharacterBody set: draining brings this set's entities up
        // to date for MakeCharacters and the write-back.
        static_cast<void>(m_characterEntities.Drain());
        std::vector<Backend::CharacterId> destroyed;
        for (auto it = m_characters.begin(); it != m_characters.end();)
        {
            // Kept only while enabled and valid: one disabled, or gone inactive by a field edit (a mesh Collider),
            // keeps no backend character, so nothing sees it.
            if (!changed.contains(it->first) && ShouldBeCharacter(m_entities->GetEntity(it->first), m_backend))
            {
                ++it;
                continue;
            }
            destroyed.push_back(it->second.id);
            m_poses.ForgetCharacter(it->first);
            it = m_characters.erase(it);
        }
        m_backend.DestroyCharacters(destroyed);
    }

    void PhysicsWorld::Impl::MakeCharacters()
    {
        std::vector<std::uint32_t> entities;
        std::vector<Backend::CharacterDesc> descs;
        std::erase_if(m_inactiveCharacters, [this](std::uint32_t id) { return !m_characterEntities.Contains(id); });
        for (std::uint32_t id : m_characterEntities.Entities())
        {
            const Entity entity = m_entities->GetEntity(id);
            const std::string why = WhyNoCharacter(entity, m_backend);
            if (why.empty())
            {
                m_inactiveCharacters.erase(id);
            }
            else if (m_inactiveCharacters.insert(id).second)
            {
                m_logger->Warn("The CharacterBody of entity " + std::to_string(id) + " is inactive: " + why +
                               "; it is a character again once it can be");
            }
            if (m_characters.contains(id) || !ShouldBeCharacter(entity, m_backend))
            {
                continue;
            }
            const Transform world = GetWorldTransform(entity);
            entities.push_back(id);
            descs.push_back(Backend::CharacterDesc{.shape = entity.GetComponent<Collider>().shape,
                                                   .scale = world.scale,
                                                   .material = MaterialOf(entity),
                                                   .filter = Detail::FilterOf(entity),
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
                                                     .scale = descs[i].scale,
                                                     .material = descs[i].material,
                                                     .filter = descs[i].filter});
            }
        }
    }

    void PhysicsWorld::Impl::PushCharacterEdits()
    {
        for (auto& [id, record] : m_characters)
        {
            const Entity entity = m_entities->GetEntity(id);
            const PhysicsMaterial material = MaterialOf(entity);
            if (!Same(material, record.material))
            {
                m_backend.SetCharacterMaterial(record.id, material);
                record.material = material;
            }
            const CollisionFilter filter = Detail::FilterOf(entity);
            if (!Same(filter, record.filter))
            {
                m_backend.SetCharacterFilter(record.id, filter);
                record.filter = filter;
            }

            const Transform world = GetWorldTransform(entity);
            if (!Same(world.pos, record.world.pos))
            {
                // Its contacts and ground are found again where it now is, so this tick's move starts from there.
                m_backend.TeleportCharacter(record.id, world.pos, world.rot);
                record.moved.carriedVelocity = Math::Vec3(0.0f);
                m_poses.SnapCharacter(id);
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
                                                                          .pushForce = body.pushForce,
                                                                          .mass = body.mass,
                                                                          .stopOnSlope = body.stopOnSlope},
                                                   dt);
        }
    }

    void PhysicsWorld::Impl::ApplyShape(const Entity& entity, CharacterRecord& record)
    {
        const CollisionShape& shape = entity.GetComponent<Collider>().shape;
        const Math::Vec3 scale = GetWorldTransform(entity).scale;
        record.shapeBlocked = false;
        // A shape no character can use made it inactive in the drain, before the move.
        if (Same(shape, record.shape) && Same(scale, record.scale))
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

    void PhysicsWorld::Impl::ResetIdleCharacters()
    {
        for (std::uint32_t id : m_characterEntities.Entities())
        {
            if (!m_characters.contains(id))
            {
                // Disabled or inactive: only its state is written, so no ground it last stood on goes stale.
                m_entities->GetEntity(id).GetComponent<CharacterBody>().state = CharacterState{};
            }
        }
    }

    void PhysicsWorld::Impl::HandCharacterPoses()
    {
        for (const auto& [id, record] : m_characters)
        {
            m_poses.HandCharacter(id, record.moved.position);
        }
    }

    std::vector<std::span<const Backend::Touch>> PhysicsWorld::Impl::CharacterMoveContacts() const
    {
        std::vector<std::span<const Backend::Touch>> moves;
        moves.reserve(m_characters.size());
        for (const auto& [id, record] : m_characters)
        {
            moves.emplace_back(record.moved.contacts);
        }
        return moves;
    }

    const std::vector<Backend::Touch>* PhysicsWorld::Impl::CharacterMoveContactsOf(const Entity& entity) const
    {
        const auto character = m_characters.find(entity.GetId());
        return character == m_characters.end() ? nullptr : &character->second.moved.contacts;
    }

    void PhysicsWorld::Impl::ReadBackCharacter(Entity& entity, CharacterRecord& record)
    {
        const Backend::CharacterMoved& moved = record.moved;
        record.world = m_poses.CharacterStepPose(entity.GetId());

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
