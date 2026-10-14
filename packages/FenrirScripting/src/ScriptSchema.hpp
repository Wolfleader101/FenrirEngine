#pragma once

#include <cstddef>
#include <map>
#include <string>

#include <Iris/Iris.hpp>

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief What a script component or event looked like when its data was last in use, so a reload can tell whether
     * the data still means the same thing: the size and alignment, and each field's name, type, kind and offset.
     */
    struct OwnedLayout
    {
        std::string fingerprint;
        std::size_t size = 0;
        std::size_t alignment = 0;
    };

    /** @brief Layouts by name. */
    using OwnedLayouts = std::map<std::string, OwnedLayout>;

    /**
     * @brief The script components and events whose data is alive in the ECS and the App: the data a generation of the
     * script assembly leaves behind for the next one.
     */
    struct OwnedScriptData
    {
        OwnedLayouts components;
        OwnedLayouts events;
    };

    /**
     * @brief Checks one native component against its C# struct in a loaded script assembly.
     *
     * @return success, or a diagnostic naming the component and the first mismatching field
     */
    [[nodiscard]] Iris::CallResult<void> ValidateRegistration(const ComponentRegistration& registration,
                                                              const Iris::Component& component);

    /**
     * @brief Everything that must hold before a freshly loaded script assembly is kept, and then the keeping.
     *
     * It either commits the whole load or changes nothing: what the assembly defines is discovered and checked, every
     * native component and event is checked against its C# struct, and only then is what the previous generation left
     * in the ECS and the App compared with this one's definitions (unchanged data stays, anything else goes with a
     * warning) and the new definitions registered.
     *
     * @param context where the engine's native functions put what the assembly reports
     * @param registry the components and events scripts can name; gains the assembly's definitions
     * @param component the freshly loaded script assembly
     * @param owned what the ECS and the App still hold from the previous generation; updated to match this one
     */
    [[nodiscard]] Iris::CallResult<void> Reconcile(EngineContext& context, ComponentRegistry& registry,
                                                   const Iris::Component& component, OwnedScriptData& owned);

    /**
     * @brief Removes the data of every owned script component and event: they exist only while a script assembly is
     * loaded. `owned` is empty afterwards.
     */
    void DropScriptData(App& app, OwnedScriptData& owned);
} // namespace Fenrir::Detail
