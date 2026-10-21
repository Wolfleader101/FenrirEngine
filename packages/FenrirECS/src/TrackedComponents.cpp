#include "FenrirECS/TrackedComponents.hpp"

#include <map>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/EntityList.hpp"

namespace Fenrir
{
    TrackedComponents::TrackedComponents(EntityList& entities, const ComponentType& type)
        : m_watch(entities.Watch(type))
    {
        // The watch records only what happens after it is made: the components already there are found once, here.
        const ComponentType types[] = {type};
        entities.ForEachWith(types, [this](std::uint32_t entity) { m_entities.insert(entity); });
    }

    std::vector<std::uint32_t> TrackedComponents::Drain()
    {
        // The watch records every add and remove, destruction and clearing included, so an entity's last change says
        // whether it is alive and has the type now, without asking a list that may be gone.
        std::map<std::uint32_t, ComponentChange::Kind> last;
        for (const ComponentChange& change : m_watch.Drain())
        {
            last.insert_or_assign(change.entity, change.kind);
        }

        std::vector<std::uint32_t> drained;
        drained.reserve(last.size());
        for (const auto& [entity, kind] : last)
        {
            drained.push_back(entity);
            if (kind == ComponentChange::Kind::Added)
            {
                m_entities.insert(entity);
            }
            else
            {
                m_entities.erase(entity);
            }
        }
        return drained;
    }
} // namespace Fenrir
