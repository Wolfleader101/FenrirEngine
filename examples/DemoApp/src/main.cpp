#include <iostream>

#include "dotnet/coreclr_delegates.h"
#include "dotnet/hostfxr.h"

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"

#ifdef WIN32
#include <Windows.h>
#include <direct.h>

#define STR(s) L##s
#define CH(c) L##c
#define DIR_SEPARATOR L'\\'

#else
#define STR(s) s
#define CH(c) c
#define DIR_SEPARATOR '/'

#include <dlfcn.h>
#endif

#include <codecvt>
#include <locale>
#include <sstream>
#include <string>

std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

namespace
{
    hostfxr_initialize_for_runtime_config_fn init_fptr;
    hostfxr_get_runtime_delegate_fn get_delegate_fptr;
    hostfxr_close_fn close_fptr;
    hostfxr_handle cxt;

    bool load_hostfxr();
    load_assembly_and_get_function_pointer_fn get_dotnet_load_assembly(const char_t* assembly);
} // namespace

namespace
{
    // Forward declarations
    void* load_library(const char_t*);
    void* get_export(void*, const char*);

#ifdef _WINDOWS
    void* load_library(const char_t* path)
    {
        HMODULE h = ::LoadLibraryW(path);
        assert(h != nullptr);
        return (void*)h;
    }

    void* get_export(void* h, const char* name)
    {
        void* f = ::GetProcAddress((HMODULE)h, name);
        assert(f != nullptr);
        return f;
    }
#else
    void* load_library(const char_t* path)
    {
        void* h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
        assert(h != nullptr);
        return h;
    }
    void* get_export(void* h, const char* name)
    {
        void* f = dlsym(h, name);
        assert(f != nullptr);
        return f;
    }
#endif

#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

    std::string GetExecutablePath()
    {
        char buffer[1024];

#if defined(_WIN32)
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
#elif defined(__linux__)
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len == -1)
            return "";
        buffer[len] = '\0';
#elif defined(__APPLE__)
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) != 0)
            return "";
#else
#error "Unsupported operating system"
#endif

        std::string fullPath(buffer);

        // Extract the directory from the full path
        size_t lastSlash = fullPath.find_last_of("\\/");
        return fullPath.substr(0, lastSlash);
    }

    std::wstring widen(const std::string& str)
    {
        std::wostringstream wstm;
        const auto& ctfacet = std::use_facet<std::ctype<wchar_t>>(wstm.getloc());
        for (size_t i = 0; i < str.size(); ++i)
        {
            wstm << ctfacet.widen(str[i]);
        }
        return wstm.str();
    }

    std::string narrow(const std::wstring& str)
    {
        std::ostringstream stm;

        const auto& ctfacet = std::use_facet<std::ctype<wchar_t>>(stm.getloc());

        for (size_t i = 0; i < str.size(); ++i)
        {
            stm << ctfacet.narrow(str[i], 0);
        }
        return stm.str();
    }

    // <SnippetLoadHostFxr>
    // Using the nethost library, discover the location of hostfxr and get exports
    bool load_hostfxr()
    {
        // Construct the path for hostfxr based on the OS
        std::wstring hostfxr_path;
#if _WIN64
        hostfxr_path = std::wstring(L"C:\\Program Files\\dotnet\\host\\fxr\\8.0.0\\hostfxr.dll");
#elif _WIN32
        hostfxr_path = std::wstring(L"C:\\Program Files (x86)\\dotnet\\host\\fxr\\8.0.0\\hostfxr.dll");
#else
        // Adjust the path for non-Windows systems
        hostfxr_path = "/usr/local/share/dotnet/host/fxr/8.0.0/libhostfxr.so"; // Example path, adjust as needed
#endif

        std::cout << "Loading hostfxr from: " << narrow(hostfxr_path) << std::endl;

        // Load hostfxr and get desired exports
        void* lib = load_library(hostfxr_path.c_str());
        init_fptr = (hostfxr_initialize_for_runtime_config_fn)get_export(lib, "hostfxr_initialize_for_runtime_config");
        if (init_fptr == nullptr)
        {
            // CRITICAL("unable to get export function: \"hostfxr_initialize_for_runtime_config\"");
            std::cout << "unable to get export function: \"hostfxr_initialize_for_runtime_config\"" << std::endl;
            return false;
        }
        get_delegate_fptr = (hostfxr_get_runtime_delegate_fn)get_export(lib, "hostfxr_get_runtime_delegate");
        if (!get_delegate_fptr)
        {
            // CRITICAL("unable to get export function: \"hostfxr_get_runtime_delegate\"");
            std::cout << "unable to get export function: \"hostfxr_get_runtime_delegate\"" << std::endl;
            return false;
        }
        close_fptr = (hostfxr_close_fn)get_export(lib, "hostfxr_close");
        if (!close_fptr)
        {
            // CRITICAL("unable to get export function: \"hostfxr_close\"");
            std::cout << "unable to get export function: \"hostfxr_close\"" << std::endl;
            return false;
        }

        return (init_fptr && get_delegate_fptr && close_fptr);
    }
    // </SnippetLoadHostFxr>

    // <SnippetInitialize>
    // Load and initialize .NET Core and get desired function pointer for scenario
    load_assembly_and_get_function_pointer_fn get_dotnet_load_assembly(const char_t* config_path)
    {
        // Load .NET Core
        void* load_assembly_and_get_function_pointer = nullptr;
        int rc = init_fptr(config_path, nullptr, &cxt);
        if (rc != 0 || cxt == nullptr)
        {
            // CRITICAL("Init failed: {0:x}", rc);
            close_fptr(cxt);
            return nullptr;
        }

        // Get the load assembly function pointer
        rc =
            get_delegate_fptr(cxt, hdt_load_assembly_and_get_function_pointer, &load_assembly_and_get_function_pointer);
        if (rc != 0 || load_assembly_and_get_function_pointer == nullptr)
        {
            // ERROR("Get delegate failed: {0:x}", rc);
        }

        // close_fptr(cxt);
        return (load_assembly_and_get_function_pointer_fn)load_assembly_and_get_function_pointer;
    }

} // namespace

static void systemA(Fenrir::App& app)
{
    app.Logger()->Warn("System A {0}", 1);
}

static void systemB(Fenrir::App& app)
{
    app.Logger()->Error("System B");
}

static void Tick(Fenrir::App& app)
{
    app.Logger()->Log("Tick");
}

static void PreInit(Fenrir::App& app)
{
    app.Logger()->Info("PreInit");
}

static void Init(Fenrir::App& app)
{
    app.Logger()->Info("Init");

    const std::string base_dir = GetExecutablePath();

    app.Logger()->Info("Loading .NET runtime...");

    if (!load_hostfxr())
    {
        app.Logger()->Error("Failed to initialize .NET runtime.");
        return;
    }

    app.Logger()->Info(".NET Runtime Initialised.");
#if _WIN32
    const auto wide_str = std::wstring(widen(base_dir) + L"\\Fenrir.Managed.runtimeconfig.json");
    app.Logger()->Info("Loading Managed Code, Runtime config: {}", narrow(wide_str).c_str());
#else
    std::string wide_str = std::string((base_dir + "\\Fenrir.Managed.runtimeconfig.json").c_str());
    app.Logger()->Info("Loading Managed Code, Runtime Config: {}", wide_str);
#endif

    const auto load_assembly_and_get_function_pointer = get_dotnet_load_assembly(wide_str.c_str());
    if (load_assembly_and_get_function_pointer == nullptr)
    {
        app.Logger()->Error("Failed to load runtime config.");
        return;
    }

#if _WIN32
    const auto dotnetlib_path = std::wstring(widen(base_dir) + L"\\Fenrir.Managed.dll");
    app.Logger()->Info("Fenrir Managed DLL: {}", narrow(dotnetlib_path));
#else
    const std::string dotnetlib_path = std::string((base_dir + "\\Fenrir.Managed.dll").c_str());
#endif
    const auto dotnet_type = STR("Fenrir.Managed.Lib, Fenrir.Managed");
    const auto dotnet_type_method = STR("Hello");
    // Namespace, assembly name

    typedef int(CORECLR_DELEGATE_CALLTYPE * custom_entry_point_fn)();
    custom_entry_point_fn entry_point = nullptr;
    const int rc = load_assembly_and_get_function_pointer(dotnetlib_path.c_str(), dotnet_type, dotnet_type_method,
                                                          //   UNMANAGEDCALLERSONLY_METHOD,
                                                          nullptr, // No delegate type name needed for static methods
                                                          nullptr, reinterpret_cast<void**>(&entry_point));

    if (rc != 0 || entry_point == nullptr)
    {
        app.Logger()->Error("Failed to get entry point for 'Hello'.");
        return;
    }

    int result = entry_point();
    if (result != 0)
    {
        app.Logger()->Error("Hello method returned failure.");
    }
    else
    {
        app.Logger()->Info("Fenrir.Managed Loaded Successfully.");
    }

    app.Logger()->Info("Fenrir.Managed Loaded Successfully.");
    return;
}

static void PostInit(Fenrir::App& app)
{
    app.Logger()->Fatal("PostInit");
}

int main()
{
    auto logger = std::make_unique<Fenrir::ConsoleLogger>();
    Fenrir::App app(std::move(logger));

    app.AddSystems(Fenrir::SchedulePriority::PreInit, {PreInit})
        .AddSystems(Fenrir::SchedulePriority::Init, {Init})
        // .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        // .AddSystems(Fenrir::SchedulePriority::Update, {systemB})
        // .AddSystems(Fenrir::SchedulePriority::PreUpdate, {systemA})
        // .AddSystems(Fenrir::SchedulePriority::Tick, {Tick})
        .Run();
}