#pragma once

#include "CallResult.hpp"

#include <memory>
#include <string>

namespace Iris
{
    struct TypeInfo
    {
        std::string fullName;
        std::string assemblyName;
    };

    /**
     * @brief A handle to a managed type found in a Component.
     *
     * Cheap to copy; it holds no managed reference. The handle is valid while its Component is loaded:
     * after the component is released, every call returns InvalidHandle. Handles are never reused, and
     * a handle only works with the component it was found in. It does not keep the component alive and
     * may outlive both the Component owner object and the Host.
     */
    class Type
    {
      public:
        [[nodiscard]] CallResult<TypeInfo> GetInfo() const;

      private:
        friend class Component;
        struct State;
        explicit Type(std::shared_ptr<const State> state) noexcept;
        std::shared_ptr<const State> state;
    };
} // namespace Iris
