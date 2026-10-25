#pragma once

#include <memory>

#include "FenrirScripting/ComponentRegistry.hpp"
#include "InputScriptingState.hpp"

namespace Fenrir
{
    /**
     * @brief The registration of the InputClaims C# reads: FenrirInput's InputClaims keeps its windows in a vector,
     * which C# cannot read in place, so C# reads a plain-data copy of it (Detail::ScriptInputClaims), an app resource
     * of this library's refreshed from InputClaims each time C# gets it. Scripts run on the thread that runs the App,
     * in sequential systems, and only FenrirInput's sequential dispatch and router calls change the claims, so the
     * refresh never races a reader. The rest of the copy's binding is generated: its mapping, and its C# mirror,
     * Fenrir.Input.InputClaims. `input` keeps the record of the warning given when claims are left out.
     */
    ResourceRegistration InputClaimsResource(std::shared_ptr<InputScripting> input);
} // namespace Fenrir
