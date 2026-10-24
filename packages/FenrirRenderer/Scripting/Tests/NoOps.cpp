// The no-op rules from C# (spec: "Scripting: the extensions", no-op rules; seam 7: a missing package no-oping): with no
// renderer, no camera or a stale entity or model, each function does nothing and returns false, zero or Entity.None,
// nothing throws, and a debug build warns once per native call site and reason, however often C# calls.
//
// The warnings are counted once per process, so these cases run in a process of their own, in declaration order
// (CTest's FenrirRendererScripting-NoOps runs them with --order decl).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "FenrirECS/DefaultComponents.hpp"
#include "RendererScriptingApp.hpp"

using namespace Fenrir;
using namespace RendererScriptingTests;

namespace
{
    std::size_t Warnings(const RendererScriptingApp& test, const std::string& text)
    {
        return LoggedWith(test.headless, Testing::LogLevel::Warn, text);
    }
} // namespace

TEST_CASE("With no renderer, picking, bounds and stats from C# do nothing and say so once",
          "[renderer][scripting][noops]")
{
    RendererScriptingApp test(false);
    Entity camera = test.Entities().CreateEntity();
    camera.AddComponent<Camera>();

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": each function should return false, zero or Entity.None");
        REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "Ray", camera.GetId(), 640.0f, 360.0f));
        REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "ScreenPoint", camera.GetId(), 0.0f, 0.0f, 0.0f));
        REQUIRE(test.Call<std::uint32_t>(RendererCallsType, "CameraAt", std::uint32_t{1}, 640.0f, 360.0f) == noEntity);
        REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "Bounds", camera.GetId()));
        REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "Stats"));
        REQUIRE(test.Value(0) == 0.0f);
        REQUIRE(test.Value(1) == 0.0f);
        REQUIRE(test.Value(2) == 0.0f);
    }
    INFO("Each function should warn once in a debug build, and never in a release build, that there is no renderer");
    for (const char* function : {"Renderer.ScreenPointToRay", "Renderer.WorldToScreenPoint", "Renderer.CameraAt",
                                 "Renderer.GetWorldBounds", "Renderer.Stats"})
    {
        INFO(function);
        REQUIRE(Warnings(test, std::string(function) + ": the app has no renderer") == warningsPerCallSite);
    }
}

TEST_CASE("SpawnModel from C# needs no renderer", "[renderer][scripting][noops]")
{
    RendererScriptingApp test(false);
    const Entity parent = test.Entities().CreateEntity();
    const auto root = test.Call<std::uint32_t>(RendererCallsType, "SpawnUnder", parent.GetId());
    test.ApplyChanges();
    INFO("The model should spawn under the parent with no renderer installed");
    REQUIRE(test.Entities().HasEntity(root));
    REQUIRE(test.Entities().GetEntity(root).GetComponent<Relationship>().parent == parent);
}

TEST_CASE("A stale entity, an entity with no Camera, an empty model or a dead parent do nothing and say so once",
          "[renderer][scripting][noops]")
{
    RendererScriptingApp test;
    const std::uint32_t dead = test.Entities().CreateEntity().GetId();
    test.Entities().DestroyEntity(dead);
    const Entity plain = test.Entities().CreateEntity();

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": each function should return false or Entity.None");
        REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "Ray", dead, 640.0f, 360.0f));
        REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "Ray", plain.GetId(), 640.0f, 360.0f));
        REQUIRE_FALSE(test.Call<bool>(RendererCallsType, "Bounds", dead));
        REQUIRE(test.Call<std::uint32_t>(RendererCallsType, "SpawnEmptyModel") == noEntity);
        REQUIRE(test.Call<std::uint32_t>(RendererCallsType, "SpawnUnder", dead) == noEntity);
    }
    {
        INFO("Nothing should have been recorded to spawn");
        REQUIRE(ScriptingProbe::PendingChanges(test.scripting) == 0);
    }
    INFO("Each reason should warn once in a debug build, and never in a release build");
    REQUIRE(Warnings(test, "Renderer.ScreenPointToRay: the entity is not alive") == warningsPerCallSite);
    REQUIRE(Warnings(test, "Renderer.ScreenPointToRay: the entity has no Camera") == warningsPerCallSite);
    REQUIRE(Warnings(test, "Renderer.GetWorldBounds: the entity is not alive") == warningsPerCallSite);
    REQUIRE(Warnings(test, "Renderer.SpawnModel: the model handle is empty or stale") == warningsPerCallSite);
    REQUIRE(Warnings(test, "Renderer.SpawnModel: the entity is not alive") == warningsPerCallSite);
}
