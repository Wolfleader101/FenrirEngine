#include "FenrirECS/WorldTransform.hpp"

namespace Fenrir
{
    namespace
    {
        Transform LocalOf(const Entity& entity)
        {
            return entity.HasComponent<Transform>() ? entity.GetComponent<Transform>() : Transform();
        }

        Entity ParentOf(const Entity& entity)
        {
            return entity.HasComponent<Relationship>() ? entity.GetComponent<Relationship>().parent : Entity();
        }
    } // namespace

    Transform GetWorldTransform(const Entity& entity)
    {
        Transform world = LocalOf(entity);
        for (Entity parent = ParentOf(entity); parent.IsValid(); parent = ParentOf(parent))
        {
            const Transform above = LocalOf(parent);
            world.pos = above.pos + Math::Rotate(above.rot, above.scale * world.pos);
            world.rot = above.rot * world.rot;
            world.scale = above.scale * world.scale;
        }
        return world;
    }

    Math::Mat4 GetWorldMatrix(const Entity& entity)
    {
        Math::Mat4 world = TransformToMat4(LocalOf(entity));
        for (Entity parent = ParentOf(entity); parent.IsValid(); parent = ParentOf(parent))
        {
            world = TransformToMat4(LocalOf(parent)) * world;
        }
        return world;
    }

    void SetWorldTransform(const Entity& entity, const Transform& world)
    {
        if (!entity.HasComponent<Transform>())
        {
            return;
        }
        Transform& local = entity.GetComponent<Transform>();
        const Entity parent = ParentOf(entity);
        if (!parent.IsValid())
        {
            local = world;
            return;
        }
        const Transform above = GetWorldTransform(parent);
        const Math::Quat unturn = Math::Conjugate(above.rot);
        local.pos = Math::Rotate(unturn, world.pos - above.pos) / above.scale;
        local.rot = unturn * world.rot;
        local.scale = world.scale / above.scale;
    }
} // namespace Fenrir
