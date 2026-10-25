#include "InputClaimsResource.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirInput/InputRouter.hpp"
#include "FenrirInputScripting/Generated/FenrirInputMappings.hpp"
#include "FenrirInputScripting/ScriptInputClaims.hpp"

// Apart from the natives in InputScripting.cpp, which describe-api reads: this source needs the generated mappings.

namespace Fenrir
{
    namespace
    {
        // Copies `claims` into `copy`: the first MaxWindows windows. True if some were left out.
        bool CopyInputClaims(const InputClaims& claims, Detail::ScriptInputClaims& copy)
        {
            const std::vector<WindowId> windows = claims.Windows();
            const std::size_t count = std::min(windows.size(), Detail::ScriptInputClaims::MaxWindows);
            copy = Detail::ScriptInputClaims{};
            for (std::size_t index = 0; index < count; ++index)
            {
                copy.entries[index] = Detail::ScriptWindowClaims{windows[index], claims.For(windows[index])};
            }
            copy.entryCount = static_cast<std::uint32_t>(count);
            copy.gamepadClaimed = claims.Gamepad();
            return windows.size() > count;
        }
    } // namespace

    ResourceRegistration InputClaimsResource(std::shared_ptr<InputScripting> input)
    {
        using Detail::ScriptInputClaims;
        ResourceRegistration registration;
        registration.name = "Fenrir.Input.InputClaims";
        registration.typeName = std::string(Iris::StructMapping<ScriptInputClaims>::typeName);
        registration.scope = ResourceScope::App;
        registration.readOnly = true;
        registration.size = sizeof(ScriptInputClaims);
        registration.find = [input](App& app) -> void* {
            const InputClaims* claims = app.TryGetResource<InputClaims>();
            if (claims == nullptr)
            {
                return nullptr;
            }
            ScriptInputClaims* copy = app.TryGetResource<ScriptInputClaims>();
            if (copy == nullptr)
            {
                copy = &app.AddResource<ScriptInputClaims>();
            }
            if (CopyInputClaims(*claims, *copy))
            {
                input->WarnOnce(app, "InputClaims: routers claim input in more windows than C# can read (" +
                                         std::to_string(ScriptInputClaims::MaxWindows) + "), so the rest are left out");
            }
            return copy;
        };
        // Scripts cannot insert or remove a read-only resource, so these are never called.
        registration.insert = [](App&) -> void* { return nullptr; };
        registration.remove = [](App&) {};
        registration.validate = [](const Iris::Component& component) {
            return component.ValidateStruct<ScriptInputClaims>();
        };
        return registration;
    }
} // namespace Fenrir
