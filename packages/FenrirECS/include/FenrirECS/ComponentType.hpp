#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

#include "FenrirECS/DynamicComponent.hpp"

namespace Fenrir
{
    class EntityList;
    class TypeRegistry;

    namespace Detail
    {
        struct WatchRecord;
    }

    /**
     * @brief Names one kind of component, native (a C++ type) or dynamic (a DynamicComponentType), so that code which
     * knows a component only at run time can reach it through EntityList: get, has, add, remove, count, clear and
     * queries all take one.
     *
     * Whoever holds one need not know which kind it is. It holds no reference to any EntityList, so make it once and
     * use it with every scene. Cheap to copy. It carries no name: a described component's name is the type registry's
     * (ForName and NameOf ask it), and a dynamic type's name is part of its DynamicComponentType.
     *
     * Native is defined with EntityList: include "FenrirECS/EntityList.hpp" to make a native one.
     */
    class ComponentType
    {
      public:
        /** @brief The C++ component type `T`, which must be default constructible and hold data. */
        template <typename T>
        [[nodiscard]] static ComponentType Native();

        /** @brief A component type known only at run time. Nothing is checked until it is used. */
        [[nodiscard]] static ComponentType Dynamic(DynamicComponentType type);

        /**
         * @brief The component the type registered under `name` (canonical or former) is, as its package's generated
         * registration attached it (FenrirECS/TypeAttachments.hpp, ADR-0074); empty when no type is registered under
         * `name` or the type registered is not a component.
         */
        [[nodiscard]] static std::optional<ComponentType> ForName(const TypeRegistry& registry, std::string_view name);

        /**
         * @brief The canonical name of the registered component `type` is, the inverse of ForName; empty when no
         * component registered in `registry` is `type`. A dynamic type is never one: its name is its
         * DynamicComponentType's. The name lives as long as the registered description.
         */
        [[nodiscard]] static std::optional<std::string_view> NameOf(const TypeRegistry& registry,
                                                                    const ComponentType& type);

        /** @brief Bytes in one component. */
        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_size;
        }

        /** @brief Alignment of one component. */
        [[nodiscard]] std::size_t Alignment() const noexcept
        {
            return m_alignment;
        }

        /** @brief Whether this is a dynamic type rather than a C++ one. */
        [[nodiscard]] bool IsDynamic() const noexcept
        {
            return m_dynamic != nullptr;
        }

      private:
        friend class EntityList;

        // Whether `other` names the same component: the same C++ type (its storage finder, which each native type's
        // has its own of) or the same dynamic type object.
        [[nodiscard]] bool IsSameAs(const ComponentType& other) const noexcept
        {
            return m_findNativeStorage == other.m_findNativeStorage && m_hookNativeWatch == other.m_hookNativeWatch &&
                   m_dynamic == other.m_dynamic && m_size == other.m_size && m_alignment == other.m_alignment;
        }

        // What EntityList needs that only the C++ type knows: where its components are. The storage comes back as an
        // opaque pointer so that no ECS library type appears here.
        using NativeStorageFinder = void* (*)(EntityList& entities);

        // Starts (or stops) a watch's record recording the adds and removes of a native storage, which only the C++
        // type can reach the signals of.
        using NativeWatchHook = void (*)(void* storage, Detail::WatchRecord& record, bool start);

        ComponentType(NativeStorageFinder findNativeStorage, NativeWatchHook hookNativeWatch,
                      std::shared_ptr<const DynamicComponentType> dynamic, std::size_t size, std::size_t alignment)
            : m_findNativeStorage(findNativeStorage), m_hookNativeWatch(hookNativeWatch), m_dynamic(std::move(dynamic)),
              m_size(size), m_alignment(alignment)
        {
        }

        NativeStorageFinder m_findNativeStorage;
        NativeWatchHook m_hookNativeWatch;
        std::shared_ptr<const DynamicComponentType> m_dynamic;
        std::size_t m_size;
        std::size_t m_alignment;
    };

    inline ComponentType ComponentType::Dynamic(DynamicComponentType type)
    {
        const std::size_t size = type.Size();
        const std::size_t alignment = type.Alignment();
        return ComponentType(nullptr, nullptr, std::make_shared<const DynamicComponentType>(std::move(type)), size,
                             alignment);
    }
} // namespace Fenrir
