#pragma once

// The renderer scripting tests' harness (spec seam 7): an App with assets installed (its asset root FenrirRenderer's
// test fixtures) and the renderer on the null backend, or no renderer for the no-op rules, stepped one frame at a time,
// with scripting running the renderer fixture script assembly, which uses the Fenrir.Renderer contract. C++ calls the
// fixture's static methods through Iris, as the system runner calls scripts, and reads what C# did through the ECS,
// RendererProbe and the renderer's public interface.

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "Device/CommandList.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirRendererScripting/RendererScripting.hpp"
#include "FenrirScripting/Scripting.hpp"
#include "RendererProbe.hpp"

namespace RendererScriptingTests
{
    inline std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the renderer scripting tests need the fixture assembly.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    inline const std::filesystem::path fixture = FromEnvironment("FENRIR_TEST_RENDERER_FIXTURE");

    inline constexpr double oneTick = Fenrir::Testing::HeadlessApp::FrameDelta;

    // The fixture's types the tests call.
    inline constexpr const char* SceneBuilderType = "RendererScriptFixture.SceneBuilder";
    inline constexpr const char* ModelSpawnerType = "RendererScriptFixture.ModelSpawner";
    inline constexpr const char* RendererCallsType = "RendererScriptFixture.RendererCalls";
    inline constexpr const char* ResourceCallsType = "RendererScriptFixture.ResourceCalls";
    inline constexpr const char* NewCallsType = "RendererScriptFixture.NewCalls";

    // C#'s Entity.None.
    inline constexpr std::uint32_t noEntity = 0xFFFFFFFFu;

    // Calls a static method of a type in the loaded fixture through Iris.
    template <typename Result, typename... Args>
    Result Call(Fenrir::Scripting& scripting, const char* typeName, const char* method, Args... arguments)
    {
        auto* component = Fenrir::ScriptingProbe::Component(scripting);
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

    // Scripting running the fixture, with the extensions the renderer's contract builds on, then the renderer's.
    inline Fenrir::ScriptingConfig FixtureConfig()
    {
        Fenrir::ScriptingConfig config;
        config.scriptAssembly = fixture;
        config.hotReload = false;
        config.extensions = {Fenrir::Input::ScriptingExtension(), Fenrir::Assets::ScriptingExtension(),
                             Fenrir::Renderer::ScriptingExtension()};
        return config;
    }

    inline Fenrir::RendererConfig NullBackend()
    {
        Fenrir::RendererConfig config;
        config.backend = Fenrir::GraphicsBackend::Null;
        return config;
    }

    // An App with assets installed (its asset root FenrirRenderer's test fixtures), the renderer on the null backend
    // unless `withRenderer` is false, and scripting running the fixture. Members end in reverse order: scripting, the
    // renderer, the asset store's owner, then the app.
    struct RendererScriptingApp
    {
        explicit RendererScriptingApp(bool withRenderer = true)
            : assets(Fenrir::Assets::Install(headless.Get(), {std::filesystem::absolute(FENRIR_RENDERER_FIXTURES)})),
              renderer(withRenderer
                           ? std::optional<Fenrir::Renderer>(Fenrir::Renderer::Install(headless.Get(), NullBackend()))
                           : std::nullopt),
              scripting(Fenrir::Scripting::Install(headless.Get(), FixtureConfig()))
        {
            INFO("The fixture did not load with the input, assets and renderer extensions");
            REQUIRE(scripting.HasScripts());
            REQUIRE((renderer.has_value() && renderer->IsValid()) == withRenderer);
        }

        Fenrir::Testing::HeadlessApp headless;
        Fenrir::Assets assets;
        std::optional<Fenrir::Renderer> renderer;
        Fenrir::Scripting scripting;

        Fenrir::App& Get()
        {
            return headless.Get();
        }

        Fenrir::AssetStore& Store()
        {
            return assets.Store();
        }

        Fenrir::EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        void Frame()
        {
            headless.Get().RunFrame(oneTick);
        }

        // Plays back what the fixture's calls recorded, as the system runner does after a system.
        void ApplyChanges()
        {
            (void)Fenrir::ScriptingProbe::ApplyChanges(scripting);
        }

        template <typename Result, typename... Args>
        Result Call(const char* typeName, const char* method, Args... arguments)
        {
            return RendererScriptingTests::Call<Result>(scripting, typeName, method, arguments...);
        }

        // The float `index` of the value the last RendererCalls call kept.
        float Value(int index)
        {
            return Call<float>(RendererCallsType, "Value", index);
        }

        [[nodiscard]] const std::vector<Fenrir::Graphics::CommandList>& Submitted() const
        {
            return Fenrir::RendererProbe::Submitted(*renderer);
        }

        [[nodiscard]] Fenrir::Graphics::GraphicsDevice& Device() const
        {
            return Fenrir::RendererProbe::Device(*renderer);
        }
    };

    // Whether two floats are exactly equal: for values that crossed between C++ and C# unchanged.
    inline bool Exactly(float a, float b)
    {
        return std::equal_to<float>{}(a, b);
    }

    // The float at byte `offset` of `bytes`.
    inline float FloatAt(const std::vector<std::byte>& bytes, std::size_t offset)
    {
        float value = 0.0f;
        REQUIRE(offset + sizeof(float) <= bytes.size());
        std::memcpy(&value, std::span(bytes).subspan(offset).data(), sizeof(float));
        return value;
    }

    // One indexed draw the device received in a pass, with the binding groups bound for it.
    struct RecordedDraw
    {
        std::vector<Fenrir::Graphics::BindingGroupHandle> groups = std::vector<Fenrir::Graphics::BindingGroupHandle>(4);
        Fenrir::Graphics::CmdDrawIndexed draw;
    };

    // Every indexed draw in passes labelled `pass`, in order.
    inline std::vector<RecordedDraw> DrawsIn(const std::vector<Fenrir::Graphics::CommandList>& lists,
                                             std::string_view pass)
    {
        using namespace Fenrir::Graphics;
        std::vector<RecordedDraw> draws;
        for (const CommandList& list : lists)
        {
            bool inPass = false;
            RecordedDraw bound;
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    inPass = begin->pass.label == pass;
                    bound = {};
                }
                else if (const auto* group = std::get_if<CmdSetBindingGroup>(&command))
                {
                    bound.groups.at(group->set) = group->group;
                }
                else if (const auto* draw = std::get_if<CmdDrawIndexed>(&command); draw != nullptr && inPass)
                {
                    RecordedDraw recorded = bound;
                    recorded.draw = *draw;
                    draws.push_back(recorded);
                }
            }
        }
        return draws;
    }

    // The bytes of the buffer bound at `binding` of binding group `group`; empty if there is none.
    inline std::vector<std::byte> BoundBytes(const Fenrir::Graphics::GraphicsDevice& device,
                                             Fenrir::Graphics::BindingGroupHandle group, std::uint32_t binding,
                                             std::size_t* offset = nullptr)
    {
        for (const Fenrir::Graphics::BindingGroupEntry& entry : Fenrir::RendererProbe::Entries(device, group))
        {
            if (entry.binding == binding)
            {
                if (offset != nullptr)
                {
                    *offset = entry.offset;
                }
                return Fenrir::RendererProbe::Bytes(device, entry.buffer);
            }
        }
        return {};
    }

    // How many messages of `level` the app logged that contain `text`.
    inline std::size_t LoggedWith(const Fenrir::Testing::HeadlessApp& headless, Fenrir::Testing::LogLevel level,
                                  const std::string& text)
    {
        std::size_t count = 0;
        for (const Fenrir::Testing::LoggedMessage& message : headless.Logger().Messages())
        {
            count += message.level == level && message.text.find(text) != std::string::npos ? 1 : 0;
        }
        return count;
    }

    // A release build warns about nothing; a debug build once per native call site.
#ifdef NDEBUG
    inline constexpr std::size_t warningsPerCallSite = 0;
#else
    inline constexpr std::size_t warningsPerCallSite = 1;
#endif
} // namespace RendererScriptingTests
