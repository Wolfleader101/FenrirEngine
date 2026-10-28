// A native component holding a tagged union: validated against its C# mirror, round-tripped through a C# system,
// the wrong-kind view's exception and its message, and a mirror with a wrong offset or name refused at load.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "TestExtension.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    void ANativeTaggedUnionValidatesAgainstItsCSharpMirror()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(),
                                            ConfigFor(fixture, {MakeComponent<TestNative::Collider>("Test.Collider")}));
        {
            INFO("The tagged union was refused: " + Errors(headless));
            REQUIRE((scripting.HasScripts() && scripting.FindComponent("Test.Collider") != nullptr));
        }
    }

    // A C# system finds a sphere (set here), reads its radius, makes it a box by assigning the kind by value, and edits
    // the box through its view; reading the box view of the sphere first throws.
    void ATaggedUnionRoundTripsThroughACSharpSystem()
    {
        HeadlessApp headless;
        // Scripting is released in the Exit phase, so what C# saw is read by an Exit system registered first.
        Scripting* installed = nullptr;
        float seenRadius = -1.0F;
        headless.Get().AddSequentialSystems(SchedulePriority::Exit, {[&](App&) {
                                                seenRadius = CallIn<float>(ShapeProbeType, *installed, "SeenRadius");
                                            }});
        auto scripting = Scripting::Install(headless.Get(),
                                            ConfigFor(fixture, {MakeComponent<TestNative::Collider>("Test.Collider")}));
        installed = &scripting;
        {
            INFO("The tagged union was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity();
        entity.AddComponent<TestNative::Collider>().shape = TestNative::SphereShape{0.25F};
        CallIn<void>(ShapeProbeType, scripting, "Enable");

        headless.Run(1);

        {
            INFO("C# did not read the sphere C++ wrote");
            REQUIRE(seenRadius == 0.25F);
        }
        auto& shape = entity.GetComponent<TestNative::Collider>().shape;
        {
            INFO("The kind C# assigned did not reach C++");
            REQUIRE(shape.Type() == TestNative::ShapeType::Box);
        }
        const auto halfExtents = shape.As<TestNative::BoxShape>().HalfExtents;
        {
            INFO("The box C# assigned and edited through its view did not reach C++");
            REQUIRE((halfExtents.X == 1.0F && halfExtents.Y == 2.0F && halfExtents.Z == 1.0F));
        }
        {
            INFO("The system failed: " + Errors(headless));
            REQUIRE(Errors(headless).empty());
        }
    }

    void ReadingATaggedUnionViewForTheWrongKindThrows()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        // The C# method reads a sphere's box view and returns the InvalidOperationException's message, or null.
        const auto message = CallIn<std::optional<std::string>>(ShapeProbeType, scripting, "WrongKindMessage");
        {
            INFO("Reading a sphere as a box did not throw InvalidOperationException");
            REQUIRE(message.has_value());
        }
        {
            INFO("The exception did not say which kind the shape is: " + *message);
            REQUIRE((message->find("Sphere") != std::string::npos && message->find("Box") != std::string::npos));
        }
    }

    // The [TaggedUnion] generator's message calls the union by its noun, or by its type's name when it has none.
    void AGeneratedTaggedUnionsMessageNamesItsNounOrItsType()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto withNoun = CallIn<std::optional<std::string>>(ShapeProbeType, scripting, "WrongKindMessage");
        {
            INFO("The message of a union with Noun = \"shape\" was: " + withNoun.value_or("(no exception)"));
            REQUIRE(withNoun == std::optional<std::string>{"The shape is a Sphere, not a Box."});
        }
        const auto withoutNoun =
            CallIn<std::optional<std::string>>(ShapeProbeType, scripting, "WrongKindMessageWithoutNoun");
        {
            INFO("The message of the union Solid, with no noun, was: " + withoutNoun.value_or("(no exception)"));
            REQUIRE(withoutNoun == std::optional<std::string>{"The Solid is a Sphere, not a Box."});
        }
    }

    // Installs the fixture with a component whose C# mirror is wrong, and checks the load fails naming the component
    // and the field (and, for an offset, where each side puts it).
    void ExpectTheMirrorRefused(const ComponentRegistration& registration, const std::string& field,
                                const std::string& detail)
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {registration}));
        {
            INFO("A wrong tagged-union mirror left scripts loaded");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The refusal did not name the component, the field and what is wrong: " + Errors(headless));
            REQUIRE((headless.Logger().Contains(LogLevel::Error, registration.name) &&
                     headless.Logger().Contains(LogLevel::Error, field) &&
                     headless.Logger().Contains(LogLevel::Error, detail)));
        }
    }

    void ATaggedUnionMirrorWithAWrongOffsetOrNameIsRefusedAtLoad()
    {
        // C#'s _sphere is at offset 8; the union is at 4.
        ExpectTheMirrorRefused(MakeComponent<TestNative::Collider, TestNative::ColliderLayout<TestNative::Misplaced>>(
                                   "Test.MisplacedCollider"),
                               "'_sphere'", "offset 8 but the native field is at 4");
        // C# calls the sphere _ball.
        ExpectTheMirrorRefused(MakeComponent<TestNative::Collider, TestNative::ColliderLayout<TestNative::Misnamed>>(
                                   "Test.MisnamedCollider"),
                               "'_ball'", "'_sphere'");
    }

} // namespace

TEST_CASE("a native tagged union validates against its C# mirror", "[scripting]")
{
    ANativeTaggedUnionValidatesAgainstItsCSharpMirror();
}

TEST_CASE("a tagged union round-trips through a C# system", "[scripting]")
{
    ATaggedUnionRoundTripsThroughACSharpSystem();
}

TEST_CASE("reading a tagged union's view for the wrong kind throws", "[scripting]")
{
    ReadingATaggedUnionViewForTheWrongKindThrows();
}

TEST_CASE("a generated tagged union's message names its noun, or its type without one", "[scripting]")
{
    AGeneratedTaggedUnionsMessageNamesItsNounOrItsType();
}

TEST_CASE("a tagged-union mirror with a wrong offset or name is refused at load", "[scripting]")
{
    ATaggedUnionMirrorWithAWrongOffsetOrNameIsRefusedAtLoad();
}
