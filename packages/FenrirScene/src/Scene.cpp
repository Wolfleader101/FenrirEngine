#include "FenrirScene/Scene.hpp"

namespace Fenrir
{
    Scene::Scene(const std::string& name) : m_name(name)
    {
    }

    const std::string& Scene::GetName() const
    {
        return m_name;
    }

    void Scene::SetName(const std::string& name)
    {
        m_name = name;
    }

    const EntityList& Scene::GetEntityList() const
    {
        return m_entityList;
    }

    EntityList& Scene::GetEntityList()
    {
        return m_entityList;
    }
} // namespace Fenrir