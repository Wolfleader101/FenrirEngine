#include "FenrirECS/ComponentWatch.hpp"

namespace Fenrir
{
    ComponentWatch& ComponentWatch::operator=(ComponentWatch&& other) noexcept
    {
        if (this != &other)
        {
            Stop();
            m_record = std::move(other.m_record);
        }
        return *this;
    }

    ComponentWatch::~ComponentWatch()
    {
        Stop();
    }

    std::vector<ComponentChange> ComponentWatch::Drain()
    {
        std::vector<ComponentChange> drained;
        if (m_record != nullptr)
        {
            drained.swap(m_record->changes);
        }
        return drained;
    }

    void ComponentWatch::Stop() noexcept
    {
        if (m_record != nullptr && m_record->hook != nullptr && !m_record->list.expired())
        {
            m_record->hook(m_record->storage, *m_record, false);
        }
        m_record.reset();
    }
} // namespace Fenrir
