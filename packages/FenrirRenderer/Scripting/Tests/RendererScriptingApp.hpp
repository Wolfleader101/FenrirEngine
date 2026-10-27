#pragma once

// The renderer scripting tests' part of FenrirScripting's ExtensionTestApp (spec seam 7): the fixture CTest names in
// FENRIR_TEST_RENDERER_FIXTURE, assets installed with its asset root FenrirRenderer's test fixtures, the renderer on
// the null backend, and the input, assets and renderer extensions. C++ calls the fixture's static methods through Iris
// and reads what C# did through the ECS, RendererProbe and the renderer's public interface.

#include "ExtensionTestApp.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "Device/CommandList.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirRendererScripting/RendererScripting.hpp"
#include "RendererProbe.hpp"

namespace RendererScriptingTests
{
    using Fenrir::Testing::noEntity;

    // The fixture's types the tests call.
    inline constexpr const char* SceneBuilderType = "RendererScriptFixture.SceneBuilder";
    inline constexpr const char* ModelSpawnerType = "RendererScriptFixture.ModelSpawner";
    inline constexpr const char* RendererCallsType = "RendererScriptFixture.RendererCalls";
    inline constexpr const char* ResourceCallsType = "RendererScriptFixture.ResourceCalls";
    inline constexpr const char* NewCallsType = "RendererScriptFixture.NewCalls";

    inline Fenrir::RendererConfig NullBackend()
    {
        Fenrir::RendererConfig config;
        config.backend = Fenrir::GraphicsBackend::Null;
        return config;
    }

    // Assets with its asset root FenrirRenderer's test fixtures, then the renderer on the null backend over it.
    struct RendererPackages
    {
        explicit RendererPackages(Fenrir::App& app)
            : assets(Fenrir::Assets::Install(app, {std::filesystem::absolute(FENRIR_RENDERER_FIXTURES)})),
              renderer(Fenrir::Renderer::Install(app, NullBackend()))
        {
        }

        Fenrir::Assets assets;
        Fenrir::Renderer renderer;
    };

    // An App with assets and the renderer installed and scripting running the fixture with the extensions the
    // renderer's contract builds on, then the renderer's.
    struct RendererScriptingApp : Fenrir::Testing::ExtensionTestApp<RendererPackages>
    {
        RendererScriptingApp()
            : ExtensionTestApp(Fenrir::Testing::FixtureFromEnvironment("FENRIR_TEST_RENDERER_FIXTURE"),
                               {Fenrir::Input::ScriptingExtension(), Fenrir::Assets::ScriptingExtension(),
                                Fenrir::Renderer::ScriptingExtension()})
        {
            INFO("The renderer did not install on the null backend");
            REQUIRE(renderer.IsValid());
        }

        Fenrir::AssetStore& Store()
        {
            return assets.Store();
        }

        // The float `index` of the value the last RendererCalls call kept.
        float Value(int index)
        {
            return Call<float>(RendererCallsType, "Value", index);
        }

        [[nodiscard]] const std::vector<Fenrir::Graphics::CommandList>& Submitted() const
        {
            return Fenrir::RendererProbe::Submitted(renderer);
        }

        [[nodiscard]] Fenrir::Graphics::GraphicsDevice& Device() const
        {
            return Fenrir::RendererProbe::Device(renderer);
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
} // namespace RendererScriptingTests
