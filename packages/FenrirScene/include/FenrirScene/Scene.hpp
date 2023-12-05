#pragma once

#include <string>

#include "FenrirECS/EntityList.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir
{
    class Scene
    {
        Scene(Fenrir::ILogger& logger, const std::string& name = "Untitled Scene") : m_logger(logger), m_name(name)
        {
        }

      private:
        std::string m_name;

        EntityList m_entityList;

        Fenrir::ILogger& m_logger;
    };
} // namespace Fenrir