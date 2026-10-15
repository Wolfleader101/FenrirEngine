// PROTOTYPE (physics spike): see WorldTransform.hpp.

#include "FenrirECS/WorldTransform.hpp"

#include <glm/gtc/quaternion.hpp>

namespace Fenrir
{
    namespace
    {
        Transform Compose(const Transform& parent, const Transform& local)
        {
            Transform world;
            world.pos = parent.pos + parent.rot * (parent.scale * local.pos);
            world.rot = glm::normalize(parent.rot * local.rot);
            world.scale = parent.scale * local.scale;
            return world;
        }
    } // namespace

    Transform GetWorldTransform(const Entity& entity)
    {
        const Transform& local = entity.GetComponent<Transform>();
        const Entity parent = entity.GetComponent<Relationship>().parent;
        if (!parent.IsValid())
        {
            return local;
        }
        return Compose(GetWorldTransform(parent), local);
    }

    void SetWorldTransform(const Entity& entity, const Transform& world)
    {
        Transform& local = entity.GetComponent<Transform>();
        const Entity parent = entity.GetComponent<Relationship>().parent;
        if (!parent.IsValid())
        {
            local = world;
            return;
        }

        const Transform parentWorld = GetWorldTransform(parent);
        const Math::Quat inverseRot = glm::inverse(parentWorld.rot);
        local.pos = (inverseRot * (world.pos - parentWorld.pos)) / parentWorld.scale;
        local.rot = glm::normalize(inverseRot * world.rot);
        local.scale = world.scale / parentWorld.scale;
    }
} // namespace Fenrir
