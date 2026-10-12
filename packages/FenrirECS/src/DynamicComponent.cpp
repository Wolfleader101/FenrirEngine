#include "FenrirECS/DynamicComponent.hpp"

#include <utility>

namespace Fenrir
{
    namespace
    {
        // 32-bit FNV-1a, with a prefix that keeps these ids apart from the ones the ECS derives from C++ types.
        std::uint32_t HashName(const std::string& name)
        {
            std::uint32_t hash = 2166136261U;
            const auto mix = [&hash](char character) {
                hash ^= static_cast<std::uint8_t>(character);
                hash *= 16777619U;
            };
            for (const char character : std::string("fenrir.dynamic:"))
            {
                mix(character);
            }
            for (const char character : name)
            {
                mix(character);
            }
            return hash;
        }
    } // namespace

    DynamicComponentType::DynamicComponentType(std::string name, std::size_t size, std::size_t alignment)
        : m_name(std::move(name)), m_size(size), m_alignment(alignment), m_id(HashName(m_name))
    {
    }

    bool DynamicComponentType::IsValid() const noexcept
    {
        return !m_name.empty() && m_size > 0 && m_size <= MaxSize && m_alignment > 0 && (m_alignment & (m_alignment - 1)) == 0 &&
               m_size % m_alignment == 0;
    }
} // namespace Fenrir
