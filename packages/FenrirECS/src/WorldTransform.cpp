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

    Transform ComposeTransform(const Transform& parent, const Transform& local)
    {
        Transform composed = local;
        composed.pos = parent.pos + Math::Rotate(parent.rot, parent.scale * local.pos);
        composed.rot = parent.rot * local.rot;
        composed.scale = parent.scale * local.scale;
        return composed;
    }

    Transform GetWorldTransform(const Entity& entity)
    {
        Transform world = LocalOf(entity);
        for (Entity parent = ParentOf(entity); parent.IsValid(); parent = ParentOf(parent))
        {
            world = ComposeTransform(LocalOf(parent), world);
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
