#include "HostFxrLibrary.hpp"

#include <mutex>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Fenrir::Scripting::Detail
{
    namespace
    {
#if defined(_WIN32)
        using ModuleHandle = HMODULE;
#else
        using ModuleHandle = void*;
#endif

        std::string Utf8Path(const std::filesystem::path& path)
        {
            const auto text = path.u8string();
            return {reinterpret_cast<const char*>(text.data()), text.size()};
        }

#if defined(_WIN32)
        std::string WindowsError(DWORD code)
        {
            return "Windows error " + std::to_string(code) + ": " +
                   std::error_code(static_cast<int>(code), std::system_category()).message();
        }
#endif

        ModuleHandle OpenModule(const std::filesystem::path& path, std::string& diagnostic)
        {
#if defined(_WIN32)
            const auto module = LoadLibraryW(path.c_str());
            if (module == nullptr)
            {
                const DWORD code = GetLastError();
                diagnostic = "Failed to load hostfxr at '" + Utf8Path(path) + "': " + WindowsError(code);
            }
#else
            const auto module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (module == nullptr)
            {
                const char* error = dlerror();
                diagnostic = "Failed to load hostfxr at '" + Utf8Path(path) +
                             "': " + (error != nullptr ? error : "unknown dynamic loader error");
            }
#endif
            return module;
        }

        template <typename Function>
        bool ResolveExport(ModuleHandle module, const char* name, Function& function, std::string& diagnostic)
        {
#if defined(_WIN32)
            const auto symbol = GetProcAddress(module, name);
            if (symbol == nullptr)
            {
                const DWORD code = GetLastError();
                diagnostic = "Failed to resolve hostfxr export '" + std::string(name) + "': " + WindowsError(code);
                return false;
            }
#else
            // Clear a previous thread-local error before inspecting this lookup.
            dlerror();
            const auto symbol = dlsym(module, name);
            const char* error = dlerror();
            if (error != nullptr || symbol == nullptr)
            {
                diagnostic = "Failed to resolve hostfxr export '" + std::string(name) +
                             "': " + (error != nullptr ? error : "symbol has a null address");
                return false;
            }
#endif
            // The OS loader supplies a symbol address; the SDK typedef supplies its ABI.
            function = reinterpret_cast<Function>(symbol);
            return true;
        }

        struct CachedModule
        {
            std::filesystem::path path;
            ModuleHandle module = nullptr;
            std::shared_ptr<const HostFxrLibrary> library;
            std::string diagnostic;
            // Deliberately no FreeLibrary/dlclose: the OS module has process lifetime,
            // including when opening it succeeded but a required export was missing.
        };

        struct ProcessLibraries
        {
            std::mutex mutex;
            CachedModule selected;
        };

        bool SameFile(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            if (left == right)
            {
                return true;
            }
            std::error_code error;
            return std::filesystem::equivalent(left, right, error) && !error;
        }
    } // namespace

    HostFxrLoadResult LoadHostFxr(const std::filesystem::path& libraryPath)
    {
        if (!libraryPath.is_absolute())
        {
            return {{}, false, "The hostfxr library path must be absolute."};
        }

        const auto path = libraryPath.lexically_normal();
        static ProcessLibraries process;
        const std::lock_guard lock(process.mutex);

        if (process.selected.module != nullptr)
        {
            const auto& cached = process.selected;
            if (SameFile(cached.path, path))
            {
                return {cached.library, true, cached.diagnostic};
            }
            return {{},
                    false,
                    "A different hostfxr library is already resident in this process: '" + Utf8Path(cached.path) +
                        "'. Restart the process to select another installation."};
        }

        CachedModule entry;
        entry.path = path;
        entry.module = OpenModule(path, entry.diagnostic);
        if (entry.module == nullptr)
        {
            // An opening failure can be retried later; no OS module was acquired.
            return {{}, false, std::move(entry.diagnostic)};
        }

        auto library = std::make_shared<HostFxrLibrary>();
        if (ResolveExport(entry.module, "hostfxr_initialize_for_runtime_config", library->initializeForRuntimeConfig,
                          entry.diagnostic) &&
            ResolveExport(entry.module, "hostfxr_get_runtime_delegate", library->getRuntimeDelegate,
                          entry.diagnostic) &&
            ResolveExport(entry.module, "hostfxr_close", library->close, entry.diagnostic) &&
            ResolveExport(entry.module, "hostfxr_set_error_writer", library->setErrorWriter, entry.diagnostic))
        {
            entry.library = std::move(library);
        }

        // Opening fixes the process's module choice, even if exports are incomplete.
        // nethost can reuse an already loaded hostfxr on subsequent discovery calls.
        process.selected = std::move(entry);
        const auto& cached = process.selected;
        return {cached.library, true, cached.diagnostic};
    }
} // namespace Fenrir::Scripting::Detail
