// PROTOTYPE (prototype/fenrir-physics-character-spike, ticket 37): a physics world's character bodies (ADR-0014), kept
// in step with the CharacterBody components and moved in PostTick step 3b. Knows the ECS, not Jolt: it talks to
// Backend::World.

#include <cstdint>
#include <string>

#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "Same.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        bool SameFilter(const CollisionFilter& a, const CollisionFilter& b)
        {
            return a.layers == b.layers && a.mask == b.mask;
        }

        // Why a character body cannot have a backend character; null if it can.
        const char* WhyCharacterInactive(const Entity& entity, Backend::World& backend)
        {
            if (entity.HasComponent<RigidBody>())
            {
                return "its entity has a RigidBody, which works instead";
            }
            if (!entity.HasComponent<Collider>())
            {
                return "its entity has no Collider";
            }
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;
            if (shape.Is<MeshShape>())
            {
                return "a character cannot have a triangle mesh shape";
            }
            if (!backend.CanMakeShape(shape))
            {
                return "its collider's collision mesh is released or cannot be used as its kind of shape";
            }
            return nullptr;
        }
    } // namespace

    bool PhysicsWorld::Impl::IsCharacterCollider(std::uint32_t entity) const
    {
        const Entity collider = m_entities->GetEntity(entity);
        return collider.HasComponent<CharacterBody>() && !collider.HasComponent<RigidBody>() &&
               !collider.HasComponent<Trigger>();
    }

    void PhysicsWorld::Impl::SyncCharacters()
    {
        for (const ComponentChange& change : m_characterWatch.Drain())
        {
            if (m_entities->HasEntity(change.entity) &&
                m_entities->GetEntity(change.entity).HasComponent<CharacterBody>())
            {
                m_characterEntities.insert(change.entity);
            }
            else
            {
                m_characterEntities.erase(change.entity);
            }
        }
        for (auto it = m_characters.begin(); it != m_characters.end();)
        {
            if (m_characterEntities.contains(it->first))
            {
                ++it;
                continue;
            }
            if (it->second.Active())
            {
                m_backend.DestroyCharacter(it->second.id);
            }
            it = m_characters.erase(it);
        }

        for (std::uint32_t id : m_characterEntities)
        {
            Entity entity = m_entities->GetEntity(id);
            const CharacterBody& body = entity.GetComponent<CharacterBody>();
            CharacterRecord& record = m_characters[id];
            const auto drop = [this, &record]() {
                if (record.Active())
                {
                    m_backend.DestroyCharacter(record.id);
                    record.id = Backend::CharacterId{};
                }
                record.carried = Math::Vec3(0.0f);
                record.moved = Backend::CharacterMoved{};
                record.snap = true;
            };

            if (const char* why = WhyCharacterInactive(entity, m_backend))
            {
                drop();
                if (!record.inactive)
                {
                    m_logger->Warn("The CharacterBody of entity " + std::to_string(id) + " is inactive: " + why);
                }
                record.inactive = true;
                continue;
            }
            record.inactive = false;
            if (!body.enabled)
            {
                drop();
                continue;
            }

            const Transform world = GetWorldTransform(entity);
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;
            const CollisionFilter filter =
                entity.HasComponent<CollisionFilter>() ? entity.GetComponent<CollisionFilter>() : CollisionFilter{};
            if (record.Active() && Same(shape, record.shape) && Same(world.scale, record.scale) &&
                SameFilter(filter, record.filter))
            {
                continue;
            }
            // Made, or remade for a new shape, scale or filter: the spike remakes rather than reshapes.
            if (record.Active())
            {
                m_backend.DestroyCharacter(record.id);
            }
            else
            {
                record.snap = true;
            }
            record.id =
                m_backend.CreateCharacter(Backend::CharacterDesc{shape, world.scale, filter, world.pos, world.rot, id});
            record.shape = shape;
            record.scale = world.scale;
            record.filter = filter;
            record.world = world;
        }
    }

    void PhysicsWorld::Impl::MoveCharacters(float dt)
    {
        for (auto& [id, record] : m_characters)
        {
            record.moving = false;
            if (!record.Active())
            {
                continue;
            }
            Entity entity = m_entities->GetEntity(id);
            const CharacterBody& body = entity.GetComponent<CharacterBody>();
            const Transform world = GetWorldTransform(entity);
            if (!Same(world.pos, record.world.pos))
            {
                // A teleport: no sweep, contacts and ground found at once, velocity kept.
                m_backend.TeleportCharacter(record.id, world.pos, world.rot);
                record.carried = Math::Vec3(0.0f);
                record.snap = true;
            }
            else if (!Same(world.rot, record.world.rot))
            {
                m_backend.TurnCharacter(record.id, world.rot);
            }
            record.world = world;

            const bool wasOnGround = record.moved.ground == CharacterGround::OnGround;
            record.moved = m_backend.MoveCharacter(record.id,
                                                   Backend::CharacterMove{.velocity = body.velocity,
                                                                          .carried = record.carried,
                                                                          .maxSlope = body.maxSlope,
                                                                          .stepHeight = body.stepHeight,
                                                                          .floorSnap = body.floorSnap,
                                                                          .stopOnSlope = body.stopOnSlope},
                                                   dt);
            record.moving = true;
            const CharacterGround ground = record.moved.ground;
            if (ground == CharacterGround::OnGround)
            {
                record.carried = Math::Vec3(0.0f);
            }
            else if (wasOnGround && (ground == CharacterGround::InAir || ground == CharacterGround::NotSupported))
            {
                // Left its ground: the ground's velocity from the move before goes with it, whole.
                record.carried = record.moved.groundVelocity;
            }
            else
            {
                record.carried = record.moved.carried;
            }
        }
    }

    void PhysicsWorld::Impl::WriteBackCharacters()
    {
        for (auto& [id, record] : m_characters)
        {
            if (!record.moving)
            {
                continue;
            }
            Entity entity = m_entities->GetEntity(id);
            Transform world = record.world;
            world.pos = record.moved.position;
            SetWorldTransform(entity, world);
            record.world = GetWorldTransform(entity);
            entity.GetComponent<CharacterBody>().velocity = record.moved.velocity;
        }
    }

    void PhysicsWorld::Impl::AdvanceCharacterPoses()
    {
        for (auto& [id, record] : m_characters)
        {
            record.previous = record.snap ? record.world : record.current;
            record.current = record.world;
            record.snap = !record.Active();
            record.heldByGame = false;
        }
    }

    void PhysicsWorld::Impl::RestoreCharacters()
    {
        for (auto& [id, record] : m_characters)
        {
            if (!record.blended)
            {
                continue;
            }
            record.blended = false;
            if (!m_entities->HasEntity(id))
            {
                continue;
            }
            Transform& local = m_entities->GetEntity(id).GetComponent<Transform>();
            if (Same(local.pos, record.shownLocal.pos) && Same(local.rot, record.shownLocal.rot) &&
                Same(local.scale, record.shownLocal.scale))
            {
                local = record.trueLocal;
            }
            else
            {
                record.snap = true;
                record.heldByGame = true;
            }
        }
    }

    void PhysicsWorld::Impl::BlendCharacters(float fraction)
    {
        for (auto& [id, record] : m_characters)
        {
            if (!record.Active() || record.heldByGame || !m_entities->HasEntity(id))
            {
                continue;
            }
            Entity entity = m_entities->GetEntity(id);
            if (!entity.GetComponent<CharacterBody>().interpolate)
            {
                continue;
            }
            record.trueLocal = entity.GetComponent<Transform>();
            Transform world = record.world;
            world.pos = record.previous.pos + (record.current.pos - record.previous.pos) * fraction;
            world.rot = Math::Slerp(record.previous.rot, record.current.rot, fraction);
            SetWorldTransform(entity, world);
            record.shownLocal = entity.GetComponent<Transform>();
            record.blended = true;
        }
    }

    CharacterState PhysicsWorld::Impl::CharacterStateOf(const Entity& character) const
    {
        if (!character.IsValid())
        {
            return CharacterState{};
        }
        const auto it = m_characters.find(character.GetId());
        if (it == m_characters.end() || !it->second.Active())
        {
            return CharacterState{};
        }
        const CharacterRecord& record = it->second;
        const Backend::CharacterMoved& moved = record.moved;
        return CharacterState{.ground = moved.ground,
                              .groundBody =
                                  moved.groundEntity == ~0u ? Entity() : m_entities->HandleOf(moved.groundEntity),
                              .groundNormal = moved.groundNormal,
                              .groundVelocity = moved.groundVelocity,
                              .carriedVelocity = record.carried,
                              .movedVelocity = moved.movedVelocity};
    }

    CharacterState PhysicsWorld::GetCharacterState(const Entity& character) const
    {
        return m_impl->CharacterStateOf(character);
    }
} // namespace Fenrir
