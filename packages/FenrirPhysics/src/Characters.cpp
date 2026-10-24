#include "Characters.hpp"

#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "DebugCanvas.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FilterOf.hpp"
#include "Poses.hpp"
#include "Same.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // The length of a character's ground normal.
        constexpr float groundNormal = 0.5f;

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

    Characters::Characters(EntityList& entities, ILogger& logger, Backend::World& backend, Poses& poses)
        : m_entities(&entities), m_backend(&backend), m_poses(&poses),
          m_characterEntities(entities, ComponentType::Native<CharacterBody>()),
          m_inactive(logger, [](std::uint32_t entity, const std::string& why) {
              return "The CharacterBody of entity " + std::to_string(entity) + " is inactive: " + why +
                     "; it is a character again once it can be";
          })
    {
    }

    void Characters::Drop(const std::set<std::uint32_t>& changed)
    {
        // Drained only to bring this set's live entities up to date for Make and the write-back. The touched entities
        // acted on come from Bodies' `changed`, drained from its own CharacterBody set, which also covers Collider and
        // RigidBody.
        static_cast<void>(m_characterEntities.Drain());
        std::vector<Backend::CharacterId> destroyed;
        for (auto it = m_characters.begin(); it != m_characters.end();)
        {
            // Kept only while enabled and valid: one disabled, or gone inactive by a field edit (a mesh Collider),
            // keeps no backend character, so nothing sees it.
            if (!changed.contains(it->first) && ShouldBeCharacter(m_entities->GetEntity(it->first), *m_backend))
            {
                ++it;
                continue;
            }
            destroyed.push_back(it->second.id);
            m_poses->Forget(it->first);
            it = m_characters.erase(it);
        }
        m_backend->DestroyCharacters(destroyed);
    }

    void Characters::Make()
    {
        std::vector<std::uint32_t> entities;
        std::vector<Backend::CharacterDesc> descs;
        m_inactive.Keep(m_characterEntities.Entities());
        for (std::uint32_t id : m_characterEntities.Entities())
        {
            const Entity entity = m_entities->GetEntity(id);
            m_inactive.Report(id, WhyNoCharacter(entity, *m_backend));
            if (m_characters.contains(id) || !ShouldBeCharacter(entity, *m_backend))
            {
                continue;
            }
            const Transform world = GetWorldTransform(entity);
            entities.push_back(id);
            descs.push_back(Backend::CharacterDesc{.shape = entity.GetComponent<Collider>().shape,
                                                   .scale = world.scale,
                                                   .material = MaterialOf(entity),
                                                   .filter = FilterOf(entity),
                                                   .position = world.pos,
                                                   .rotation = world.rot,
                                                   .entity = id});
        }
        std::vector<Backend::CharacterId> ids(descs.size());
        m_backend->CreateCharacters(descs, ids);
        for (std::size_t i = 0; i < descs.size(); ++i)
        {
            if (ids[i].value != Backend::CharacterId{}.value)
            {
                m_characters.emplace(entities[i], CharacterRecord{.id = ids[i],
                                                                  .moved = Backend::CharacterMoved{},
                                                                  .shape = descs[i].shape,
                                                                  .scale = descs[i].scale,
                                                                  .material = descs[i].material,
                                                                  .filter = descs[i].filter});
                m_poses->Made(entities[i], GetWorldTransform(m_entities->GetEntity(entities[i])));
            }
        }
    }

    void Characters::PushEdits()
    {
        for (auto& [id, record] : m_characters)
        {
            const Entity entity = m_entities->GetEntity(id);
            const PhysicsMaterial material = MaterialOf(entity);
            if (!Same(material, record.material))
            {
                m_backend->SetCharacterMaterial(record.id, material);
                record.material = material;
            }
            const CollisionFilter filter = FilterOf(entity);
            if (!Same(filter, record.filter))
            {
                m_backend->SetCharacterFilter(record.id, filter);
                record.filter = filter;
            }

            const Transform world = GetWorldTransform(entity);
            const Poses::Edit edit = m_poses->Push(id, Poses::Kind::Character, world);
            if (edit == Poses::Edit::Moved)
            {
                // Its contacts and ground are found again where it now is, so this tick's move starts from there.
                m_backend->TeleportCharacter(record.id, world.pos, world.rot);
                record.moved.carriedVelocity = Math::Vec3(0.0f);
            }
            else if (edit == Poses::Edit::Turned)
            {
                // Not a teleport: the next move pushes the turned shape out of anything it now overlaps.
                m_backend->TurnCharacter(record.id, world.rot);
            }
        }
    }

    void Characters::Move(float dt)
    {
        for (auto& [id, record] : m_characters)
        {
            const Entity entity = m_entities->GetEntity(id);
            ApplyShape(entity, record);
            const CharacterBody& body = entity.GetComponent<CharacterBody>();
            record.moved = m_backend->MoveCharacter(record.id,
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

    void Characters::ApplyShape(const Entity& entity, CharacterRecord& record)
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
        if (!m_backend->ReshapeCharacter(record.id, shape, scale, onGround))
        {
            // The Collider keeps what game code asked for, and the next move tries it again.
            record.shapeBlocked = true;
            return;
        }
        record.shape = shape;
        record.scale = scale;
    }

    void Characters::ResetIdle()
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

    std::vector<std::span<const Backend::Touch>> Characters::MoveContacts() const
    {
        std::vector<std::span<const Backend::Touch>> moves;
        moves.reserve(m_characters.size());
        for (const auto& [id, record] : m_characters)
        {
            moves.emplace_back(record.moved.contacts);
        }
        return moves;
    }

    const std::vector<Backend::Touch>* Characters::MoveContactsOf(const Entity& entity) const
    {
        const auto character = m_characters.find(entity.GetId());
        return character == m_characters.end() ? nullptr : &character->second.moved.contacts;
    }

    void Characters::HandPoses()
    {
        const auto handle = [this](std::uint32_t id) { return id == ~0u ? Entity() : m_entities->HandleOf(id); };
        for (auto& [id, record] : m_characters)
        {
            const Backend::CharacterMoved& moved = record.moved;
            Transform world;
            world.pos = moved.position;
            m_poses->Hand(id, Poses::Kind::Character, world);

            CharacterBody& body = m_entities->GetEntity(id).GetComponent<CharacterBody>();
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
    }

    void Characters::Draw(DebugCanvas& canvas) const
    {
        // Only characters with a backend character: disabled and inactive ones have no record, and draw nothing.
        for (const auto& [id, record] : m_characters)
        {
            // The shape it has, not the Collider's: while a shape edit waits, that is the one bodies and queries see.
            canvas.Collider(record.shape, Transform(Math::Vec3(0.0f), Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), record.scale),
                            m_poses->Baseline(id), PhysicsDebugColour::Character);

            const Backend::CharacterMoved& moved = record.moved;
            if (moved.ground == GroundState::OnGround || moved.ground == GroundState::OnSteepGround)
            {
                const Colour& colour = moved.ground == GroundState::OnGround ? PhysicsDebugColour::Ground
                                                                             : PhysicsDebugColour::SteepGround;
                canvas.Line(moved.groundPoint, moved.groundPoint + groundNormal * moved.groundNormal, colour);
            }
            if (moved.movedVelocity != Math::Vec3(0.0f))
            {
                const Math::Vec3& at = m_poses->Baseline(id).pos;
                canvas.Line(at, at + velocitySeconds * moved.movedVelocity, PhysicsDebugColour::Velocity);
            }
        }
    }
} // namespace Fenrir::Detail
