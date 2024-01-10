#include "FenrirECS/Entity.hpp"

#include "FenrirECS/DefaultComponents.hpp"

#include <string>

namespace Fenrir
{
    Entity::Entity(uint32_t id, EntityList* entityList)
        : m_entityId(static_cast<entt::entity>(id)), m_entityList(entityList)
    {
        AddComponent<Transform>();
        AddComponent<Name>();
        AddComponent<Relationship>();

        std::string name = "Entity " + std::to_string(id);
        GetComponent<Name>().Set(name.c_str());
    }

    bool Entity::operator==(const Entity& other) const
    {
        return this->m_entityId == other.m_entityId && this->m_entityList == other.m_entityList;
    }

    bool Entity::operator!=(const Entity& other) const
    {
        return !(*this == other);
    }

    uint32_t Entity::GetId() const
    {
        return static_cast<uint32_t>(m_entityId);
    }

    bool Entity::IsValid() const
    {
        return m_entityId != entt::null && m_entityList->m_registry.valid(m_entityId);
    }

    void Entity::AddChild(Entity child) const
    {
        // if the child is not valid return
        if (!child.IsValid())
        {
            return;
        }

        Relationship& relationship = GetComponent<Relationship>();

        // if there is no first child, set the first child to the child
        if (!relationship.firstChild.IsValid())
        {
            relationship.firstChild = child;
            child.GetComponent<Relationship>().parent = *this;
            return;
        }

        // if there is a first child, find the last child and set the next sibling to the child
        Entity lastChild = relationship.firstChild;
        while (lastChild.GetComponent<Relationship>().nextSibling.IsValid())
        {
            lastChild = lastChild.GetComponent<Relationship>().nextSibling;
        }

        // set the next sibling of the last child to the child
        lastChild.GetComponent<Relationship>().nextSibling = child;
        child.GetComponent<Relationship>().prevSibling = lastChild;
        child.GetComponent<Relationship>().parent = *this;
    }

    void Entity::RemoveChild(Entity child) const
    {

        // if the child entity is not valid or is not a child of this entity, return
        if (!child.IsValid() || child.GetComponent<Relationship>().parent != *this)
            return;

        Relationship& relationship = GetComponent<Relationship>();

        if (!relationship.firstChild.IsValid())
            return;

        Entity prevSibling;
        Entity current = relationship.firstChild;

        // iterate the children until we find the child or reach the end of the list
        while (current.IsValid() && current != child)
        {
            prevSibling = current;
            current = current.GetComponent<Relationship>().nextSibling;
        }

        // if the child is not found, return
        if (!current.IsValid() || current != child)
            return;

        // if the child is the first child
        if (relationship.firstChild == child)
        {
            relationship.firstChild = child.GetComponent<Relationship>().nextSibling;
        }
        else
        {
            // update the previous sibling next sibling
            prevSibling.GetComponent<Relationship>().nextSibling = child.GetComponent<Relationship>().nextSibling;
        }

        // update the next sibling previous sibling
        if (child.GetComponent<Relationship>().nextSibling.IsValid())
        {
            child.GetComponent<Relationship>().nextSibling.GetComponent<Relationship>().prevSibling = prevSibling;
        }

        // clear the child's relationship links
        child.GetComponent<Relationship>().parent = Entity();
        child.GetComponent<Relationship>().nextSibling = Entity();
        child.GetComponent<Relationship>().prevSibling = Entity();
    }

} // namespace Fenrir