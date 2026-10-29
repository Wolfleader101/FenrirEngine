// A shader's parameters from C# (spec: "Scripting: the extensions", Fenrir.Assets): Shaders.GetParameterCount and
// GetParameter list every parameter with its type, default and name, the name filling a span and the whole length
// returned.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "AssetsScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

TEST_CASE("Shaders.GetParameter lists every parameter with its type, default and name", "[assets][scripting]")
{
    AssetsScriptingApp test;
    {
        INFO("C# does not count the fixture shader's five parameters");
        REQUIRE(test.Call<int>(ShaderCallsType, "Count", BitsOf(test.shader)) == 5);
    }
    // The fixture's parameters (FixtureParameters), types as ShaderParameterType's values (Float 0 to Texture 4) and
    // default textures as DefaultTexture's (White 0, Normal 2).
    INFO("C# does not list the fixture shader's parameters in order, each with its type, default and name");
    REQUIRE(test.Text(ShaderCallsType, "List", BitsOf(test.shader)) == "tint 3 1,0.5,0.25,1 0\n"
                                                                       "roughness 0 0.5,0,0,0 0\n"
                                                                       "offset 1 2,3,0,0 0\n"
                                                                       "emissive 2 0,0,0,0 0\n"
                                                                       "baseColourTexture 4 0,0,0,0 2\n");
}

TEST_CASE("Shaders.GetParameter fills as much of the name as fits and returns its whole length", "[assets][scripting]")
{
    AssetsScriptingApp test;
    {
        INFO("Into a span of 64 chars, the fifth parameter's name did not come whole");
        REQUIRE(test.Call<int>(ShaderCallsType, "NameLength", BitsOf(test.shader), 4, 64) == 17);
        REQUIRE(test.Text(ShaderCallsType, "Name", BitsOf(test.shader), 4, 64) == "baseColourTexture");
    }
    {
        INFO("Into a span of 4 chars, the first 4 did not come, with the whole length returned");
        REQUIRE(test.Call<int>(ShaderCallsType, "NameLength", BitsOf(test.shader), 4, 4) == 17);
        REQUIRE(test.Text(ShaderCallsType, "Name", BitsOf(test.shader), 4, 4) == "base");
    }
    INFO("An index past the parameters read as something, or left the parameter written");
    REQUIRE(test.Call<bool>(ShaderCallsType, "MissingParameterIsZeroed", BitsOf(test.shader), 5));
    REQUIRE(test.Call<bool>(ShaderCallsType, "MissingParameterIsZeroed", BitsOf(test.shader), -1));
}
