#pragma once

// The one harness for a package's .NET-driven scripting tests (spec seam 7): an App with no window, the package
// installed, stepped one frame at a time, with scripting running the package's fixture script assembly with its
// extension. C++ calls the fixture's static methods through Iris, as the system runner calls scripts, and compares what
// C# did with what the engine holds. Each package's <Package>ScriptingApp.hpp adds only its own part: the fixture CTest
// names, the extensions, the packages to install and any helper only it has.
//
// Shared with package tests through the FenrirScriptingTestSupport library, like HeadlessApp.hpp and
// ScriptingProbe.hpp.

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/Scripting.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir::Testing
{
    /**
     * @brief The fixture script assembly CTest names in the environment variable `variable`, made absolute, or an
     * empty path (saying so on stderr) when it is not set.
     */
    inline std::filesystem::path FixtureFromEnvironment(const char* variable)
    {
        const char* value = std::getenv(variable);
        if (value == nullptr)
        {
            std::cerr << "Set " << variable << " (CTest does): the scripting tests need the fixture assembly.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    /** @brief The App's default fixed step: a frame of `oneTick` seconds runs exactly one tick, two of them two. */
    inline constexpr double oneTick = HeadlessApp::FrameDelta;

    /** @brief C#'s Entity.None, as an entity id crosses to and from the fixture. */
    inline constexpr std::uint32_t noEntity = 0xFFFFFFFFu;

    /**
     * @brief Calls the static method `method` of the type `typeName` in the loaded fixture through Iris, requiring
     * each step: a loaded script assembly, the type, the method with this signature, and a call that did not fail.
     */
    template <typename Result, typename... Args>
    Result Call(Scripting& scripting, const char* typeName, const char* method, Args... arguments)
    {
        auto* component = ScriptingProbe::Component(scripting);
        {
            INFO("There is no loaded script assembly");
            REQUIRE(component != nullptr);
        }
        const auto type = component->FindType(typeName);
        {
            INFO(std::string("The fixture type was not found: ") + type.diagnostic);
            REQUIRE(type.Succeeded());
        }
        const auto handle = type.value->FindMethod<Result(Args...)>(method);
        {
            INFO(std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
            REQUIRE(handle.Succeeded());
        }
        const auto result = handle.value->Invoke(arguments...);
        {
            INFO(std::string("The fixture method ") + method + " failed: " + result.diagnostic);
            REQUIRE(result.Succeeded());
        }
        if constexpr (!std::is_void_v<Result>)
        {
            return *result.value;
        }
    }

    /** @brief Scripting running `fixture` with `extensions`, in that order, and no hot reload. */
    inline ScriptingConfig FixtureConfig(std::filesystem::path fixture, std::vector<ScriptingExtension> extensions)
    {
        ScriptingConfig config;
        config.scriptAssembly = std::move(fixture);
        config.hotReload = false;
        config.extensions = std::move(extensions);
        return config;
    }

    /** @brief How many messages of `level` the app logged that contain `text`. */
    inline std::size_t LoggedWith(const HeadlessApp& headless, LogLevel level, const std::string& text)
    {
        std::size_t count = 0;
        for (const LoggedMessage& message : headless.Logger().Messages())
        {
            count += message.level == level && message.text.find(text) != std::string::npos ? 1 : 0;
        }
        return count;
    }

    /** @brief The packages of an ExtensionTestApp that installs none: scripting alone, as the window's tests run. */
    struct NoPackages
    {
        explicit NoPackages(App&)
        {
        }
    };

    namespace Detail
    {
        /** @brief The App under test: a base, so the packages constructed after it can be installed into it. */
        struct HeadlessOwner
        {
            HeadlessApp headless;

            App& Get()
            {
                return headless.Get();
            }
        };
    } // namespace Detail

    /**
     * @brief An App with no window, `Packages` installed into it, and scripting running `fixture` with `extensions`.
     *
     * `Packages` is a package's own struct: its members are the installed packages (`Assets assets;`), constructed
     * from the App and whatever else its constructor takes after the extensions here, and they are this app's members
     * too (`test.assets`). Everything ends in reverse order: scripting, then the packages, then the app.
     */
    template <typename Packages>
    struct ExtensionTestApp : Detail::HeadlessOwner, Packages
    {
        /** @brief Scripting from `config`, for a package whose tests also start scripting from an edited config. */
        template <typename... PackageArgs>
        explicit ExtensionTestApp(const ScriptingConfig& config, PackageArgs&&... packageArgs)
            : Packages(headless.Get(), std::forward<PackageArgs>(packageArgs)...),
              scripting(Scripting::Install(headless.Get(), config))
        {
            INFO("The fixture did not load with the package's extensions: " << config.scriptAssembly.string());
            REQUIRE(scripting.HasScripts());
        }

        template <typename... PackageArgs>
        ExtensionTestApp(const std::filesystem::path& fixture, std::vector<ScriptingExtension> extensions,
                         PackageArgs&&... packageArgs)
            : ExtensionTestApp(FixtureConfig(fixture, std::move(extensions)), std::forward<PackageArgs>(packageArgs)...)
        {
        }

        Scripting scripting;

        using Detail::HeadlessOwner::Get;

        EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        /** @brief Runs one frame of `deltaTime` seconds. */
        void Frame(double deltaTime = oneTick)
        {
            headless.Get().RunFrame(deltaTime);
        }

        /** @brief Runs `frames` frames of one tick each. */
        void Run(int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                headless.Get().RunFrame(oneTick);
            }
        }

        /** @brief Plays back the structural changes the fixture recorded, as the system runner does after a system. */
        void ApplyChanges()
        {
            (void)ScriptingProbe::ApplyChanges(scripting);
        }

        /** @brief Calls a static method of a type in the loaded fixture through Iris. */
        template <typename Result, typename... Args>
        Result Call(const char* typeName, const char* method, Args... arguments)
        {
            return Testing::Call<Result>(scripting, typeName, method, arguments...);
        }
    };
} // namespace Fenrir::Testing
