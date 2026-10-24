#pragma once

// The debug draw scripting tests' harness (spec seam 7: debug draw from C#): an App with debug drawing installed (or
// not, for the no-op rules), stepped one frame at a time, with scripting running the debug draw fixture script
// assembly, which uses the Fenrir.DebugDraw contract. C++ calls the fixture's static methods through Iris, as the
// system runner calls scripts, and reads what C# drew through the active scene's DebugDraw: its tessellated batches
// (seam 6).

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirDebugDrawScripting/DebugDrawScripting.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace DebugDrawScriptingTests
{
    inline std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the debug draw scripting tests need the fixture assembly.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    inline const std::filesystem::path fixture = FromEnvironment("FENRIR_TEST_DEBUGDRAW_FIXTURE");

    // One tick's worth of frame time, and a frame too short to run a tick.
    inline constexpr double oneTick = Fenrir::Testing::HeadlessApp::FrameDelta;
    inline constexpr double noTick = oneTick * 0.25;

    // The fixture's types the tests call.
    inline constexpr const char* ShapeCallsType = "DebugDrawScriptFixture.ShapeCalls";
    inline constexpr const char* TextCallsType = "DebugDrawScriptFixture.TextCalls";
    inline constexpr const char* ItemCallsType = "DebugDrawScriptFixture.ItemCalls";
    inline constexpr const char* TickDrawerType = "DebugDrawScriptFixture.TickDrawer";

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
            INFO(std::string("The fixture method ") + method + " failed (it should never throw): " + result.diagnostic);
            REQUIRE(result.Succeeded());
        }
        if constexpr (!std::is_void_v<Result>)
        {
            return *result.value;
        }
    }

    inline Fenrir::ScriptingConfig FixtureConfig()
    {
        Fenrir::ScriptingConfig config;
        config.scriptAssembly = fixture;
        config.hotReload = false;
        config.extensions = {Fenrir::DebugDrawing::ScriptingExtension()};
        return config;
    }

    // An App with debug drawing installed unless `withDebugDrawing` is false, and scripting running the fixture. Each
    // frame's Render phase keeps what the active scene's DebugDraw tessellates to in `seen`. Members end in reverse
    // order: scripting, debug drawing, then the app.
    struct DebugDrawScriptingApp
    {
        explicit DebugDrawScriptingApp(bool withDebugDrawing = true)
            : drawing(withDebugDrawing
                          ? std::optional<Fenrir::DebugDrawing>(Fenrir::DebugDrawing::Install(headless.Get()))
                          : std::nullopt),
              scripting(Fenrir::Scripting::Install(headless.Get(), FixtureConfig()))
        {
            INFO("The fixture did not load with the debug draw extension");
            REQUIRE(scripting.HasScripts());
            headless.Get().AddSequentialSystem(Fenrir::SchedulePriority::Render, [this](Fenrir::App& app) {
                if (const Fenrir::DebugDraw* draw = app.GetActiveScene().GetResource<Fenrir::DebugDraw>())
                {
                    seen = draw->Tessellate();
                }
            });
        }

        Fenrir::Testing::HeadlessApp headless;
        std::optional<Fenrir::DebugDrawing> drawing;
        Fenrir::Scripting scripting;
        Fenrir::DebugBatches seen;

        Fenrir::App& Get()
        {
            return headless.Get();
        }

        Fenrir::EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        // The active scene's DebugDraw.
        Fenrir::DebugDraw& Draw()
        {
            Fenrir::DebugDraw* draw = headless.Get().GetActiveScene().GetResource<Fenrir::DebugDraw>();
            REQUIRE(draw != nullptr);
            return *draw;
        }

        // Runs one frame of `deltaTime` seconds and returns what its Render phase saw.
        const Fenrir::DebugBatches& Frame(double deltaTime = oneTick)
        {
            seen = {};
            headless.Get().RunFrame(deltaTime);
            return seen;
        }

        template <typename Result, typename... Args>
        Result Call(const char* typeName, const char* method, Args... arguments)
        {
            return DebugDrawScriptingTests::Call<Result>(scripting, typeName, method, arguments...);
        }
    };

    // How many lines every line batch holds together.
    inline std::size_t LineCount(const Fenrir::DebugBatches& batches)
    {
        std::size_t count = 0;
        for (const Fenrir::DebugLineBatch& batch : batches.lines)
        {
            count += batch.vertices.size() / 2;
        }
        return count;
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
} // namespace DebugDrawScriptingTests
