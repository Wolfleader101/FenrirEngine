#pragma once

#include <string>

#include "FenrirECS/EntityList.hpp"

namespace Fenrir
{
    class ILogger;
    class Scene
    {
        Scene(Fenrir::ILogger& logger, const std::string& name);

        const std::string& GetName() const;

        void SetName(const std::string& name);

        const EntityList& GetEntityList() const;

        EntityList& GetEntityList();

      private:
        std::string m_name;

        EntityList m_entityList;

        Fenrir::ILogger& m_logger;
    };
} // namespace Fenrir