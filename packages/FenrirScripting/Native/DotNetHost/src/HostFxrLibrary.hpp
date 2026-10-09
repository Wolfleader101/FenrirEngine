#pragma once

#include <hostfxr.h>

#include <filesystem>
#include <memory>
#include <string>

namespace Fenrir::Scripting::Detail
{
    // Exact ABI signatures from the SDK, kept outside the public host interface.
    struct HostFxrLibrary
    {
        hostfxr_initialize_for_runtime_config_fn initializeForRuntimeConfig = nullptr;
        hostfxr_get_runtime_delegate_fn getRuntimeDelegate = nullptr;
        hostfxr_close_fn close = nullptr;
        hostfxr_set_error_writer_fn setErrorWriter = nullptr;
    };

    struct HostFxrLoadResult
    {
        // Published only when every required export has been resolved.
        std::shared_ptr<const HostFxrLibrary> library;
        bool moduleLoaded = false;
        std::string diagnostic;
    };

    // Successful OS module loads are never unloaded. Repeated requests reuse the
    // cached module/table. The first successfully opened module fixes the choice,
    // even if its exports are incomplete; changing installations requires a restart.
    [[nodiscard]] HostFxrLoadResult LoadHostFxr(const std::filesystem::path& libraryPath);
} // namespace Fenrir::Scripting::Detail
