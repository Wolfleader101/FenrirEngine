// Fenrir.Math from C#: each type laid out as its FenrirMath counterpart, a matrix crossing column by column, the
// defaults and sRGB decoding matching C++, and the assembly being the host's shared copy.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirScripting/Generated/FenrirScriptingMappings.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;
    using Catch::Approx;

    template <typename Native>
    void ExpectTheMirrorMatches(const Iris::Component& component, const char* name)
    {
        const auto checked = component.ValidateStruct<Native>();
        INFO(std::string("The C# ") + name + " should be laid out as its C++ counterpart: " + checked.diagnostic);
        REQUIRE(checked.Succeeded());
    }

    void EachFenrirMathTypeIsLaidOutAsItsFenrirMathCounterpart()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const Iris::Component* component = ScriptingProbe::Component(scripting);
        {
            INFO("The fixture did not load");
            REQUIRE(component != nullptr);
        }
        ExpectTheMirrorMatches<glm::vec2>(*component, "Fenrir.Math.Vector2");
        ExpectTheMirrorMatches<glm::vec3>(*component, "Fenrir.Math.Vector3");
        ExpectTheMirrorMatches<glm::vec4>(*component, "Fenrir.Math.Vector4");
        ExpectTheMirrorMatches<glm::quat>(*component, "Fenrir.Math.Quaternion");
        ExpectTheMirrorMatches<Math::Mat4>(*component, "Fenrir.Math.Matrix4x4");
        ExpectTheMirrorMatches<Colour>(*component, "Fenrir.Math.Colour");
        ExpectTheMirrorMatches<Math::Ray>(*component, "Fenrir.Math.Ray");
        ExpectTheMirrorMatches<Math::AABB>(*component, "Fenrir.Math.Bounds");
        // Transform stays in the scripting contract, built from Fenrir.Math's Vector3 and Quaternion.
        ExpectTheMirrorMatches<Transform>(*component, "Fenrir.Scripting.Transform");
    }

    void AMatrixCrossesColumnByColumn()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        // A translation by (1, 2, 3) in glm's column-major matrix: the translation is the fourth column.
        const Math::Mat4 translation = Math::Translate(Math::Mat4(1.0F), Math::Vec3(1.0F, 2.0F, 3.0F));
        const Math::Mat4 matrix = translation;
        {
            INFO("C# did not read the translation in the fourth column");
            REQUIRE(Maths<float>(scripting, "MatrixElement", matrix, 3, 0) == 1.0F);
            REQUIRE(Maths<float>(scripting, "MatrixElement", matrix, 3, 1) == 2.0F);
            REQUIRE(Maths<float>(scripting, "MatrixElement", matrix, 3, 2) == 3.0F);
            REQUIRE(Maths<float>(scripting, "MatrixElement", matrix, 3, 3) == 1.0F);
        }
        {
            INFO("C# did not read the identity's diagonal and zeroes elsewhere");
            REQUIRE(Maths<float>(scripting, "MatrixElement", matrix, 0, 0) == 1.0F);
            REQUIRE(Maths<float>(scripting, "MatrixElement", matrix, 1, 1) == 1.0F);
            REQUIRE(Maths<float>(scripting, "MatrixElement", matrix, 0, 3) == 0.0F);
            REQUIRE(Maths<float>(scripting, "MatrixElement", matrix, 1, 0) == 0.0F);
        }
        {
            INFO("C#'s Matrix4x4.Identity was not the identity C++ makes");
            const auto identity = Maths<Math::Mat4>(scripting, "Identity");
            const Math::Mat4 expected(1.0F);
            REQUIRE(std::memcmp(&identity, &expected, sizeof(identity)) == 0);
        }
    }

    void NewColoursAndRaysStartAsTheirCppDefaults()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("A new C# Colour was not opaque white, as a default Fenrir::Colour is");
            const auto colour = Maths<Colour>(scripting, "NewColour");
            REQUIRE((colour.r == 1.0F && colour.g == 1.0F && colour.b == 1.0F && colour.a == 1.0F));
        }
        {
            INFO("A new C# Ray did not point along +z from the origin, as a default Math::Ray does");
            const auto ray = Maths<Math::Ray>(scripting, "NewRay");
            REQUIRE((ray.origin == Math::Vec3(0.0F) && ray.dir == Math::Vec3(0.0F, 0.0F, 1.0F)));
        }
        {
            INFO("A C# Ray made from a direction did not normalize it, as Math::Ray does");
            const auto ray = Maths<Math::Ray>(scripting, "RayAlong", 0.0F, 3.0F, 4.0F);
            REQUIRE((ray.dir.x == 0.0F && ray.dir.y == Approx(0.6F) && ray.dir.z == Approx(0.8F)));
        }
    }

    void AColourFromSrgbMatchesCpp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        const Colour expected = Colour::FromSrgb(0x336699CCU);
        const auto colour = Maths<Colour>(scripting, "FromSrgb", 0x336699CCU);
        {
            INFO("C#'s Colour.FromSrgb did not decode 0x336699CC as Fenrir::Colour::FromSrgb does");
            REQUIRE(colour.r == Approx(expected.r));
            REQUIRE(colour.g == Approx(expected.g));
            REQUIRE(colour.b == Approx(expected.b));
            REQUIRE(colour.a == Approx(expected.a));
        }
        const auto grey = Maths<Colour>(scripting, "Grey");
        {
            INFO("C#'s Colour.Grey was not Fenrir::Colour::Grey");
            REQUIRE(grey.r == Approx(Colour::Grey.r));
            REQUIRE(grey.a == 1.0F);
        }
    }

    void FenrirMathIsTheHostsSharedCopy()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("Fenrir.Math was not the host's shared copy in the default load context");
            REQUIRE(Maths<bool>(scripting, "MathIsShared"));
        }
        const auto location = Maths<std::optional<std::string>>(scripting, "MathLocation");
        {
            INFO("Fenrir.Math was not loaded from the host's directory");
            REQUIRE(location.has_value());
            REQUIRE(std::filesystem::equivalent(*location, executableDirectory / "Fenrir.Math.dll"));
        }
    }

} // namespace

TEST_CASE("each Fenrir.Math type is laid out as its FenrirMath counterpart", "[scripting][math]")
{
    EachFenrirMathTypeIsLaidOutAsItsFenrirMathCounterpart();
}

TEST_CASE("a matrix crosses to C# column by column", "[scripting][math]")
{
    AMatrixCrossesColumnByColumn();
}

TEST_CASE("a new C# Colour and Ray start as their C++ defaults", "[scripting][math]")
{
    NewColoursAndRaysStartAsTheirCppDefaults();
}

TEST_CASE("a Colour from sRGB in C# matches C++", "[scripting][math]")
{
    AColourFromSrgbMatchesCpp();
}

TEST_CASE("Fenrir.Math is the host's shared copy", "[scripting][math]")
{
    FenrirMathIsTheHostsSharedCopy();
}
