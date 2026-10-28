// The native component registry from C#: Ref<T> over the fixture's own structs, the engine's Transform, Name and
// hierarchy, a component the test registers, a read-only part, an Entity field written from C#, and what a lookup
// says when it fails.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"
#include "TestExtension.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;
    using TestNative::Health;

    // The native side of the components the fixture declares (same layout as the C# structs).
    struct NativeVec3
    {
        float X, Y, Z;
    };
    struct NativeTransform
    {
        NativeVec3 Position;
        float Scale;
        std::int32_t Flags;
    };
    struct NativeNested
    {
        double Weight;
    };
    struct NativeHidden
    {
        std::int32_t Count;
    };
    struct NativeClash
    {
        std::int32_t Value, IsNull, Fine;
    };
    struct NativeSplit
    {
        std::int32_t A, B;
    };
    struct NativeReserved
    {
        std::int32_t Class, Event;
    };

    std::uint64_t AddressOf(const void* memory)
    {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(memory));
    }

    void RefWritesThroughGeneratedAccessorsIntoNativeMemory()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }

        // t.Position.X += 1 on a nested struct field, a plain assignment, a compound multiply and a flag: all land in
        // memory this test owns, with no ref at the call site.
        NativeTransform transform{{1, 2, 3}, 4, 1};
        Access<void>(scripting, "Nudge", AddressOf(&transform));
        {
            INFO("Writes through the generated accessors did not reach native memory");
            REQUIRE((transform.Position.X == 2 && transform.Position.Y == 5 && transform.Position.Z == 3 &&
                     transform.Scale == 8 && transform.Flags == 5));
        }
        {
            INFO("Reads through the generated accessors did not see native memory");
            REQUIRE(Access<float>(scripting, "ReadSum", AddressOf(&transform)) == 18.0F);
        }

        // Writes from C++ are seen too: it is the same memory.
        transform.Position.Z = 10;
        {
            INFO("A native write was not seen by the script");
            REQUIRE(Access<float>(scripting, "ReadSum", AddressOf(&transform)) == 25.0F);
        }

        NativeNested nested{1.0};
        {
            INFO("A nested component type got no accessors");
            REQUIRE((Access<double>(scripting, "NudgeNested", AddressOf(&nested)) == 1.5 && nested.Weight == 1.5));
        }
        NativeHidden hidden{4};
        {
            INFO("An internal component got no accessors");
            REQUIRE((Access<std::int32_t>(scripting, "NudgeHidden", AddressOf(&hidden)) == 7 && hidden.Count == 7));
        }
        NativeClash clash{0, 0, 0};
        {
            INFO("Fields named like Ref<T> members were not reachable through Value");
            REQUIRE((Access<std::int32_t>(scripting, "NudgeClash", AddressOf(&clash)) == 24 && clash.Fine == 7 &&
                     clash.Value == 8 && clash.IsNull == 9));
        }
        NativeSplit split{100, 200};
        {
            INFO("A partial component did not get accessors for all of its fields");
            REQUIRE((Access<std::int32_t>(scripting, "NudgeSplit", AddressOf(&split)) == 303 && split.A == 101 &&
                     split.B == 202));
        }
        NativeReserved reserved{10, 20};
        {
            INFO("Keyword-named fields were not exposed");
            REQUIRE((Access<std::int32_t>(scripting, "NudgeReserved", AddressOf(&reserved)) == 33 &&
                     reserved.Class == 11 && reserved.Event == 22));
        }
    }

    void ANullRefSaysSoInsteadOfCrashing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("A null Ref was not reported as null");
            REQUIRE(Access<bool>(scripting, "NullRefIsNull"));
        }
        const auto message = Access<std::optional<std::string>>(scripting, "NullRefMessage");
        {
            INFO("Reading a null Ref did not throw a clear exception");
            REQUIRE((message.has_value() && message->find("Pose") != std::string::npos));
        }
    }

    // ---- the native component registry

    void ATransformIsTheSameMemoryInCppAndInCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();

        auto entity = entities.CreateEntity();
        entity.AddComponent<Transform>(Math::Vec3(1, 2, 3), Math::Quat(1, 0, 0, 0), Math::Vec3(1, 1, 1));
        const auto id = entity.GetId();

        // C++ wrote it; C# reads the same values (Rotation.W is the first float in glm's layout).
        {
            INFO("C# did not read the position C++ wrote (got " +
                 std::to_string(Registry<float>(scripting, "ReadPositionX", id)) + ")");
            REQUIRE(Registry<float>(scripting, "ReadPositionX", id) == 1.0F);
        }
        {
            INFO("C# did not read the rotation C++ wrote");
            REQUIRE(Registry<float>(scripting, "ReadRotationW", id) == 1.0F);
        }
        {
            INFO("C# did not read the scale C++ wrote");
            REQUIRE(Registry<float>(scripting, "ReadScaleZ", id) == 1.0F);
        }

        // C# writes through its Ref<Transform>; C++ sees it through GetComponent.
        Registry<void>(scripting, "Move", id);
        const auto& transform = entity.GetComponent<Transform>();
        {
            INFO("C# writes to the position were not seen");
            REQUIRE((transform.pos.x == 2 && transform.pos.y == 2 && transform.pos.z == 3));
        }
        {
            INFO("C# writes to the rotation were not seen");
            REQUIRE((transform.rot.w == 0.5F && transform.rot.x == 0 && transform.rot.y == 0 && transform.rot.z == 0));
        }
        {
            INFO("C# writes to the scale were not seen");
            REQUIRE((transform.scale.x == 3 && transform.scale.y == 3 && transform.scale.z == 3));
        }

        // And the other way round, in the same memory.
        entity.GetComponent<Transform>().pos.x = 10;
        {
            INFO("C# did not see a later C++ write");
            REQUIRE(Registry<float>(scripting, "ReadPositionX", id) == 10.0F);
        }

        // Has: live state, for an entity with and without the component (every engine entity starts with a Transform).
        auto bare = entities.CreateEntity();
        bare.RemoveComponent<Transform>();
        {
            INFO("Has<Transform> was wrong");
            REQUIRE((Registry<bool>(scripting, "HasTransform", id) &&
                     !Registry<bool>(scripting, "HasTransform", bare.GetId())));
        }
        entities.DestroyEntity(id);
        {
            INFO("Has<Transform> was true for a destroyed entity");
            REQUIRE(!Registry<bool>(scripting, "HasTransform", id));
        }
    }

    void ANameRoundTripsBothWays()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto entity = entities.CreateEntity();
        const auto id = entity.GetId(); // Every engine entity starts with a Name.

        // C++ to C#, with a multi-byte character.
        entity.GetComponent<Name>().Set("h\xC3\xA9llo");
        const auto read = Registry<std::optional<std::string>>(scripting, "ReadName", id);
        {
            INFO("C# did not read the name C++ wrote");
            REQUIRE((read.has_value() && *read == "h\xC3\xA9llo"));
        }

        // C# to C++.
        Registry<void>(scripting, "WriteName", id, std::string_view("from C# \xCF\x80"));
        {
            INFO("C++ did not read the name C# wrote");
            REQUIRE(std::string(entity.GetComponent<Name>().Get()) == "from C# \xCF\x80");
        }
        {
            INFO("C# did not read back its own name");
            REQUIRE(Registry<std::optional<std::string>>(scripting, "ReadName", id).value() == "from C# \xCF\x80");
        }

        // A name longer than the buffer is cut between characters, and stays terminated.
        std::string tooLong;
        for (int index = 0; index < 200; ++index)
        {
            tooLong += "\xC3\xA9"; // 400 bytes
        }
        Registry<void>(scripting, "WriteName", id, std::string_view(tooLong));
        const std::string stored = entity.GetComponent<Name>().Get();
        {
            INFO("A long name was not cut on a character boundary (it was " + std::to_string(stored.size()) +
                 " bytes)");
            REQUIRE((stored.size() == 254 && stored.size() % 2 == 0 && stored.substr(0, 4) == "\xC3\xA9\xC3\xA9"));
        }

        // An empty name clears the buffer.
        Registry<void>(scripting, "WriteName", id, std::string_view(""));
        {
            INFO("An empty name did not clear the buffer");
            REQUIRE(entity.GetComponent<Name>().IsEmpty());
        }
    }

    void TheHierarchyIsReadableButNotWritable()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto parent = entities.CreateEntity();
        auto first = entities.CreateEntity();
        auto second = entities.CreateEntity();
        parent.AddChild(first);
        parent.AddChild(second);

        // C# reads exactly the links C++ made (the layout of the stand-in is the layout of the real thing).
        {
            INFO("The parent link was wrong");
            REQUIRE(Registry<std::uint32_t>(scripting, "ParentOf", first.GetId()) == parent.GetId());
        }
        {
            INFO("The second parent link was wrong");
            REQUIRE(Registry<std::uint32_t>(scripting, "ParentOf", second.GetId()) == parent.GetId());
        }
        {
            INFO("The first child link was wrong");
            REQUIRE(Registry<std::uint32_t>(scripting, "FirstChildOf", parent.GetId()) == first.GetId());
        }
        {
            INFO("The sibling link was wrong");
            REQUIRE(Registry<std::uint32_t>(scripting, "NextSiblingOf", first.GetId()) == second.GetId());
        }
        {
            INFO("A missing parent was not a null link");
            REQUIRE(Registry<std::uint32_t>(scripting, "ParentOf", parent.GetId()) == NullEntity);
        }
        {
            INFO("A missing sibling was not null");
            REQUIRE(Registry<std::uint32_t>(scripting, "NextSiblingOf", second.GetId()) == NullEntity);
        }

        // It cannot be written from C#: no Ref<Relationship>, and assignments through ReadOnlyRef do not compile (see
        // the RefCompile-Relationship* tests). Native code can still change it, and C# sees that.
        parent.RemoveChild(first);
        {
            INFO("C# did not see the hierarchy change");
            REQUIRE(Registry<std::uint32_t>(scripting, "FirstChildOf", parent.GetId()) == second.GetId());
        }
    }

    void AWorldTransformIsReadAndSetFromCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto parent = entities.CreateEntity();
        auto child = entities.CreateEntity();
        parent.AddChild(child);
        // The parent sits at (10, 0, 0), turned a quarter about +Y (+X to -Z) and doubled; the child is 1 along its +X.
        parent.GetComponent<Transform>() =
            Transform(Math::Vec3(10.0F, 0.0F, 0.0F),
                      Math::AngleAxis(Math::DegToRad(90.0F), Math::Vec3(0.0F, 1.0F, 0.0F)), Math::Vec3(2.0F));
        child.GetComponent<Transform>().pos = Math::Vec3(1.0F, 0.0F, 0.0F);
        const auto near = [](float left, float right) { return std::abs(left - right) < 1e-4F; };

        const float worldZ = Registry<float>(scripting, "WorldPositionZ", child.GetId());
        {
            INFO("C# read world Z " + std::to_string(worldZ) + ", not -2");
            REQUIRE(near(worldZ, -2.0F));
        }

        // C# sets the child's world position and reads it back in the same call, before anything returns to C++.
        const float readBack = Registry<float>(scripting, "PlaceAndReadWorldY", child.GetId(), 4.0F, 6.0F, -8.0F);
        {
            INFO("C# read back world Y " + std::to_string(readBack) + " after setting 6");
            REQUIRE(near(readBack, 6.0F));
        }
        const Transform world = GetWorldTransform(child);
        {
            INFO("C++ did not see the world position C# set");
            REQUIRE((near(world.pos.x, 4.0F) && near(world.pos.y, 6.0F) && near(world.pos.z, -8.0F)));
        }
        {
            INFO("The child's local Transform was not set through its parent (expected (4, 3, -3))");
            const Math::Vec3 local = child.GetComponent<Transform>().pos;
            REQUIRE((near(local.x, 4.0F) && near(local.y, 3.0F) && near(local.z, -3.0F)));
        }
        {
            INFO("A dead entity's world transform was not refused");
            const auto message = Registry<std::optional<std::string>>(scripting, "DeadWorldTransform", NullEntity);
            REQUIRE(message.has_value());
        }
    }

    void ACustomComponentRegisteredByTheTestWorksWithNoEngineChanges()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<Health>("Test.Health")}));
        {
            INFO("The fixture did not load with a matching component");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto entity = entities.CreateEntity();
        const auto id = entity.GetId();

        // The engine knows nothing of a component nobody registered.
        const auto unregistered = Registry<std::optional<std::string>>(scripting, "NotRegistered", id);
        {
            INFO("A component nobody registered was not explained");
            REQUIRE((unregistered.has_value() && unregistered->find("Test.Nothing") != std::string::npos));
        }

        // A registered one is found, and the entity simply does not have it yet.
        const auto lookup = Registry<std::optional<std::string>>(scripting, "HealthLookup", id);
        {
            INFO("A registered component the entity lacks was not explained: " + lookup.value_or("<no exception>"));
            REQUIRE((lookup.has_value() && lookup->find("has no Health") != std::string::npos));
        }
        {
            INFO("Has<Health> was true before it was added");
            REQUIRE(!Registry<bool>(scripting, "HasHealth", id));
        }
        entity.AddComponent<Health>();
        entity.GetComponent<Health>() = Health{20, 30};
        {
            INFO("Has<Health> was false after it was added");
            REQUIRE(Registry<bool>(scripting, "HasHealth", id));
        }
        {
            INFO("C# did not read the custom component");
            REQUIRE(Registry<std::int32_t>(scripting, "HealthSum", id) == 55);
        }
        {
            INFO("C# writes to the custom component were not seen");
            REQUIRE(entity.GetComponent<Health>().Current == 25);
        }
    }

    void LookupFailuresSayWhatWentWrong()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto bare = entities.CreateEntity();
        bare.RemoveComponent<Transform>();
        auto gone = entities.CreateEntity();
        const auto goneId = gone.GetId();
        entities.DestroyEntity(goneId);

        const auto missing = Registry<std::optional<std::string>>(scripting, "MissingComponent", bare.GetId());
        {
            INFO("A missing component was not explained (got " + missing.value_or("<no exception>") + ")");
            REQUIRE((missing.has_value() && missing->find("has no Transform") != std::string::npos));
        }
        const auto dead = Registry<std::optional<std::string>>(scripting, "MissingComponent", goneId);
        {
            INFO("A dead entity was not explained");
            REQUIRE((dead.has_value() && dead->find("not alive") != std::string::npos));
        }
    }

    void AScriptCannotClaimANativeComponentWithAStructOfItsOwn()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity();

        // The fixture declares a writable component that names the hierarchy ("Fenrir.Relationship"). The engine only
        // hands out a native component to the C# type it validated, so the claim is refused.
        const auto refused = Registry<std::optional<std::string>>(scripting, "FakeHierarchy", entity.GetId());
        {
            INFO("A script's own struct got a native component: " + refused.value_or("<no exception>"));
            REQUIRE((refused.has_value() && refused->find("different C# type") != std::string::npos));
        }
    }

    // A C# system reads a reading C++ wrote through the writable Ref<Gauge> its query hands out, from the component's
    // own storage, and writes the rate from it.
    void AReadOnlyFieldIsReadThroughRefInPlace()
    {
        HeadlessApp headless;
        // Scripting is released in the Exit phase, so what C# saw is read by an Exit system registered first.
        Scripting* installed = nullptr;
        float seenLevel = -1.0F;
        std::int32_t seenSamples = -1;
        std::uint64_t seenAddress = 0;
        headless.Get().AddSequentialSystems(SchedulePriority::Exit, {[&](App&) {
                                                seenLevel = CallIn<float>(GaugeProbeType, *installed, "SeenLevel");
                                                seenSamples =
                                                    CallIn<std::int32_t>(GaugeProbeType, *installed, "SeenSamples");
                                                seenAddress =
                                                    CallIn<std::uint64_t>(GaugeProbeType, *installed, "SeenAddress");
                                            }});
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<TestNative::Gauge>("Test.Gauge")}));
        installed = &scripting;
        {
            INFO("The gauge was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity();
        entity.AddComponent<TestNative::Gauge>().reading = {0.5F, 3};
        CallIn<void>(GaugeProbeType, scripting, "Enable");

        headless.Run(1);

        auto& gauge = entity.GetComponent<TestNative::Gauge>();
        {
            INFO("C# did not read the reading C++ wrote");
            REQUIRE((seenLevel == 0.5F && seenSamples == 3));
        }
        {
            INFO("C# read a copy of the reading, not the component's storage");
            REQUIRE(seenAddress == AddressOf(&gauge.reading));
        }
        {
            INFO("The rate C# wrote through the same Ref did not reach C++");
            REQUIRE(gauge.rate == 1.5F);
        }

        {
            INFO("The system failed: " + Errors(headless));
            REQUIRE(Errors(headless).empty());
        }
    }

    ComponentRegistration TetherComponent()
    {
        return MakeComponent<TestNative::Tether>("Test.Tether");
    }

    void AnEntityFieldWrittenFromCSharpReadsCorrectlyInCpp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {TetherComponent()}));
        {
            INFO("The component with an Entity field was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto holder = entities.CreateEntity();
        auto target = entities.CreateEntity();
        holder.AddComponent<TestNative::Tether>();

        Links<void>(scripting, "LinkTo", holder.GetId(), target.GetId());

        // Equal means the same entity value in the same entity list: C# filled the owner with the active scene's.
        const Entity written = holder.GetComponent<TestNative::Tether>().target;
        {
            INFO("The Entity C# wrote is not the target in the active scene");
            REQUIRE(written == target);
        }
        {
            INFO("C++ could not use the Entity C# wrote");
            REQUIRE((written.IsValid() && written.HasComponent<Transform>()));
        }
    }

    void ALinkToAnEntityCreatedInTheSameSystemReadsBackAfterPlayback()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {TetherComponent()}));
        {
            INFO("The component with an Entity field was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();

        const auto holderId = Links<std::uint32_t>(scripting, "CreateLinkedPair");
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Playback did not create both entities and add the Tether");
            REQUIRE((summary.applied == 3 && summary.skipped == 0));
        }

        const auto targetId = Links<std::uint32_t>(scripting, "TargetOf", holderId);
        {
            INFO("C# did not read the link back after playback");
            REQUIRE((targetId != NullEntity && targetId != holderId));
        }
        const auto& tether = entities.GetEntity(holderId).GetComponent<TestNative::Tether>();
        {
            INFO("C++ did not read the link to the created entity in the active scene");
            REQUIRE((tether.target == entities.GetEntity(targetId) && tether.target.HasComponent<Transform>()));
        }
        {
            INFO("The rest of the component did not arrive");
            REQUIRE(tether.length == 3);
        }
    }

    void AZeroedLinkNamesEntityZeroWithNoSceneAndReadsSafely()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {TetherComponent()}));
        {
            INFO("The component with an Entity field was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto holder = entities.CreateEntity();
        {
            INFO("The first entity of a new scene is not entity 0, so this test cannot tell entity 0 apart");
            REQUIRE(holder.GetId() == 0);
        }
        holder.AddComponent<TestNative::Tether>();

        Links<void>(scripting, "WriteZeroed", holder.GetId());

        {
            INFO("C# did not read a zeroed link as naming entity 0");
            REQUIRE(Links<std::uint32_t>(scripting, "TargetOf", holder.GetId()) == 0);
        }
        // Entity 0 is alive in the active scene, but the zeroed link has no scene, so it is neither it nor valid.
        const Entity zeroed = holder.GetComponent<TestNative::Tether>().target;
        {
            INFO("A zeroed link read in C++ as entity 0 of the active scene");
            REQUIRE((zeroed.GetId() == 0 && zeroed != holder));
        }
        {
            INFO("A zeroed link read in C++ as valid");
            REQUIRE(!zeroed.IsValid());
        }
    }

    void AnEntityLinksOwnerIsNotReadableFromCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        // The spec's whole readable surface: whether it is empty, the entity it names, and the empty link.
        const auto members = Links<std::optional<std::string>>(scripting, "ReadableMembers");
        {
            INFO("EntityLink exposes more than IsNull, Target and None to scripts: " + members.value_or("<null>"));
            REQUIRE(members == "IsNull:Boolean,None:EntityLink,Target:Entity");
        }
    }

    void EntityLinkNoneIsTheEmptyLink()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {TetherComponent()}));
        {
            INFO("The component with an Entity field was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("EntityLink.None was not null in C#");
            REQUIRE(Links<bool>(scripting, "NoneIsNull"));
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto holder = entities.CreateEntity();
        auto target = entities.CreateEntity();
        holder.AddComponent<TestNative::Tether>().target = target;

        Links<void>(scripting, "Unlink", holder.GetId());

        // The empty link is a default Entity: the null value and no entity list.
        {
            INFO("EntityLink.None did not read in C++ as an empty Entity");
            REQUIRE(holder.GetComponent<TestNative::Tether>().target == Entity());
        }
        {
            INFO("C# did not read the empty link back as null");
            REQUIRE(Links<std::uint32_t>(scripting, "TargetOf", holder.GetId()) == NullEntity);
        }
    }

    void TheEngineComponentsComeWithTheirRules()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto* transform = scripting.FindComponent("Fenrir.Transform");
        const auto* name = scripting.FindComponent("Fenrir.Name");
        const auto* relationship = scripting.FindComponent("Fenrir.Relationship");
        {
            INFO("An engine component was not registered");
            REQUIRE((transform != nullptr && name != nullptr && relationship != nullptr));
        }
        {
            INFO("Transform or Name is not required, or is read-only");
            REQUIRE((transform->required && !transform->readOnly && name->required && !name->readOnly));
        }
        {
            INFO("Relationship is not both required and read-only");
            REQUIRE((relationship->required && relationship->readOnly));
        }
    }

} // namespace

TEST_CASE("Ref<T> writes through generated accessors into native memory", "[scripting]")
{
    RefWritesThroughGeneratedAccessorsIntoNativeMemory();
}

TEST_CASE("a null Ref says so instead of crashing", "[scripting]")
{
    ANullRefSaysSoInsteadOfCrashing();
}

TEST_CASE("a Transform is the same memory in C++ and C#", "[scripting]")
{
    ATransformIsTheSameMemoryInCppAndInCSharp();
}

TEST_CASE("a Name round-trips both ways, and long text is cut between characters", "[scripting]")
{
    ANameRoundTripsBothWays();
}

TEST_CASE("the hierarchy is readable from C# but not writable", "[scripting]")
{
    TheHierarchyIsReadableButNotWritable();
}

TEST_CASE("C# reads and sets a child's world transform and sees its own edit", "[scripting]")
{
    AWorldTransformIsReadAndSetFromCSharp();
}

TEST_CASE("a component registered by the test works with no engine changes", "[scripting]")
{
    ACustomComponentRegisteredByTheTestWorksWithNoEngineChanges();
}

TEST_CASE("lookup failures say what went wrong", "[scripting]")
{
    LookupFailuresSayWhatWentWrong();
}

TEST_CASE("a readonly field is read through Ref<T> in a C# system, in place", "[scripting]")
{
    AReadOnlyFieldIsReadThroughRefInPlace();
}

TEST_CASE("a script cannot claim a native component with a struct of its own", "[scripting]")
{
    AScriptCannotClaimANativeComponentWithAStructOfItsOwn();
}

TEST_CASE("the engine components come with their rules", "[scripting]")
{
    TheEngineComponentsComeWithTheirRules();
}

TEST_CASE("an Entity field written from C# reads correctly in C++", "[scripting]")
{
    AnEntityFieldWrittenFromCSharpReadsCorrectlyInCpp();
}

TEST_CASE("EntityLink.None is the empty link", "[scripting]")
{
    EntityLinkNoneIsTheEmptyLink();
}

TEST_CASE("a link to an entity created earlier in the same system reads back after playback", "[scripting]")
{
    ALinkToAnEntityCreatedInTheSameSystemReadsBackAfterPlayback();
}

TEST_CASE("a zeroed EntityLink names entity 0 with no scene and reads safely", "[scripting]")
{
    AZeroedLinkNamesEntityZeroWithNoSceneAndReadsSafely();
}

TEST_CASE("an EntityLink's owner is not readable from C#", "[scripting]")
{
    AnEntityLinksOwnerIsNotReadableFromCSharp();
}
