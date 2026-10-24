// Loading from C# (spec: "Scripting: the extensions", Fenrir.Assets): Assets.Load takes a path, file#Label or a
// builtin: name and gives the handle C++'s Load gives; a failed load gives the empty handle and the store logs why;
// owners are the active scene or the app; Unload frees and IsValid tells.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include "AssetsScriptingApp.hpp"

using namespace Fenrir;
using namespace AssetsScriptingTests;

TEST_CASE("A C# system loads a model by path and its mesh by file#Label, as C++ loads them", "[assets][scripting]")
{
    AssetsScriptingApp test;
    test.Call<void>(MeshBuilderType, "Arm");
    test.Get().RunFrame(oneTick);

    const auto model = HandleFrom<Model>(test.Call<std::uint64_t>(MeshBuilderType, "ModelHandle"));
    const auto mesh = HandleFrom<Mesh>(test.Call<std::uint64_t>(MeshBuilderType, "LabelledMeshHandle"));
    {
        INFO("The model C# loaded by path is not the one C++ loads from the same path");
        REQUIRE_FALSE(model.IsEmpty());
        REQUIRE(model == test.Store().Load<Model>("models/scene.gltf", AssetOwner::App()).handle);
    }
    INFO("The mesh C# loaded by file#Label is not the model's first mesh");
    const Fenrir::Model* data = test.Store().TryGet(model);
    REQUIRE(data != nullptr);
    REQUIRE_FALSE(data->meshes.empty());
    REQUIRE(mesh == data->meshes[0]);
}

TEST_CASE("C# loads a builtin: name as the handle AddBuiltin registered", "[assets][scripting]")
{
    AssetsScriptingApp test;
    {
        INFO("Loading the fixture shader's built-in name from C# did not give its handle");
        REQUIRE(HandleFrom<Shader>(test.Call<std::uint64_t>(AssetCallsType, "LoadShader",
                                                            std::string_view(FixtureShaderName))) == test.shader);
    }
    INFO("A built-in name nothing registered loaded as something");
    REQUIRE(test.Call<std::uint64_t>(AssetCallsType, "LoadShader", std::string_view("builtin:nothing-here")) == 0);
}

TEST_CASE("A failed load from C# gives the empty handle, and the store logs why", "[assets][scripting]")
{
    AssetsScriptingApp test;
    {
        INFO("A missing file loaded as something");
        REQUIRE(test.Call<std::uint64_t>(AssetCallsType, "LoadMesh", std::string_view("models/missing.gltf#Mesh0"),
                                         sceneOwner) == 0);
    }
    {
        INFO("A label the file lacks loaded as something");
        REQUIRE(test.Call<std::uint64_t>(AssetCallsType, "LoadMesh", std::string_view("models/scene.gltf#Mesh9"),
                                         sceneOwner) == 0);
    }
    INFO("The store did not log each failed load as an error naming its path");
    REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Error, "missing.gltf#Mesh0") == 1);
    REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Error, "scene.gltf#Mesh9") == 1);
}

TEST_CASE("Unload from C# frees the asset, and IsValid then reads it as stale", "[assets][scripting]")
{
    AssetsScriptingApp test;
    const auto bits =
        test.Call<std::uint64_t>(AssetCallsType, "LoadMesh", std::string_view("models/scene.gltf#Mesh0"), appOwner);
    {
        INFO("The mesh did not load, or C# does not read it as valid");
        REQUIRE(test.Store().IsValid(HandleFrom<Mesh>(bits)));
        REQUIRE(test.Call<bool>(AssetCallsType, "IsValidMesh", bits));
    }
    {
        INFO("Unloading a live mesh from C# did not succeed and free it");
        REQUIRE(test.Call<bool>(AssetCallsType, "UnloadMesh", bits));
        REQUIRE_FALSE(test.Store().IsValid(HandleFrom<Mesh>(bits)));
    }
    INFO("The stale handle reads as valid, or unloads again");
    REQUIRE_FALSE(test.Call<bool>(AssetCallsType, "IsValidMesh", bits));
    REQUIRE_FALSE(test.Call<bool>(AssetCallsType, "UnloadMesh", bits));
}

TEST_CASE("An asset C# adds for the scene ends with the active scene; one for the app outlives it",
          "[assets][scripting]")
{
    AssetsScriptingApp test;
    const std::string first = test.Get().GetActiveScene().GetName();
    test.Get().CreateScene("Level");
    test.Get().ChangeActiveScene("Level");
    const auto forScene = HandleFrom<Mesh>(test.Call<std::uint64_t>(AssetCallsType, "AddTriangle", sceneOwner));
    const auto forApp = HandleFrom<Mesh>(test.Call<std::uint64_t>(AssetCallsType, "AddTriangle", appOwner));
    {
        INFO("C# did not add both meshes");
        REQUIRE(test.Store().IsValid(forScene));
        REQUIRE(test.Store().IsValid(forApp));
    }
    test.Get().ChangeActiveScene(first);
    test.Get().DestroyScene("Level");
    INFO("The scene's mesh outlived the active scene it was added for, or the app's did not");
    REQUIRE_FALSE(test.Store().IsValid(forScene));
    REQUIRE(test.Store().IsValid(forApp));
}
