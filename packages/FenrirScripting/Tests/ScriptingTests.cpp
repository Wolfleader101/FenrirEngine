#include "HeadlessApp.hpp"
#include "TestHarness.hpp"

#include <entt/entity/runtime_view.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "../src/ByteStorage.hpp"
#include "FenrirScripting/Scripting.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <vector>
#include <fstream>
#include <iostream>
#include <optional>
#include <type_traits>
#include <span>

// A component the tests register from outside FenrirScripting, and a deliberately wrong mapping of the same C# struct.
namespace TestNative
{
    struct Health
    {
        std::int32_t Current;
        std::int32_t Max;
    };

    // The fields are in the wrong order for the C# Health.
    struct BadHealth
    {
        std::int32_t Max;
        std::int32_t Current;
    };

    // The right order, but Current has the wrong type.
    struct FloatHealth
    {
        float Current;
        std::int32_t Max;
    };
} // namespace TestNative

template <>
struct Iris::StructMapping<TestNative::Health>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Health";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestNative::Health, Current), IRIS_FIELD(TestNative::Health, Max));
};

template <>
struct Iris::StructMapping<TestNative::BadHealth>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Health";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestNative::BadHealth, Max), IRIS_FIELD(TestNative::BadHealth, Current));
};

template <>
struct Iris::StructMapping<TestNative::FloatHealth>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Health";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestNative::FloatHealth, Current), IRIS_FIELD(TestNative::FloatHealth, Max));
};

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using TestNative::BadHealth;
    using TestNative::FloatHealth;
    using TestNative::Health;

    std::filesystem::path fixture;
    std::filesystem::path executableDirectory;

    constexpr int Frames = 5;

    constexpr const char* FixtureType = "Fenrir.ScriptFixture.Fixture";

    constexpr const char* AccessType = "Fenrir.ScriptFixture.Access";
    constexpr const char* RegistryType = "Fenrir.ScriptFixture.Registry";

    // Calls a static method of a type in the fixture script assembly through Iris, as the runner will call scripts.
    template <typename Result, typename... Args>
    Result CallIn(const char* typeName, Scripting& scripting, const char* method, Args... arguments)
    {
        auto* component = scripting.GetComponent();
        Check(component != nullptr, "There is no loaded script assembly");
        const auto type = component->FindType(typeName);
        Check(type.Succeeded(), std::string("The fixture type was not found: ") + type.diagnostic);
        const auto handle = type.value->FindMethod<Result(Args...)>(method);
        Check(handle.Succeeded(), std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
        const auto result = handle.value->Invoke(arguments...);
        Check(result.Succeeded(), std::string("The fixture method ") + method + " failed: " + result.diagnostic);
        if constexpr (!std::is_void_v<Result>)
        {
            return *result.value;
        }
    }

    template <typename Result, typename... Args>
    Result Call(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(FixtureType, scripting, method, arguments...);
    }

    template <typename Result, typename... Args>
    Result Access(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(AccessType, scripting, method, arguments...);
    }

    template <typename Result, typename... Args>
    Result Registry(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(RegistryType, scripting, method, arguments...);
    }

    constexpr std::uint32_t NullEntity = 0xFFFFFFFFU;

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

    // A directory holding copies of the files Scripting looks for beside the executable, minus the ones left out.
    struct BridgeCopy
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / "fenrir-bridge-copy";
        BridgeCopy(const std::filesystem::path& from, bool withBridge, bool withContract)
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
            std::filesystem::create_directories(path);
            if (withBridge)
            {
                for (const char* name : {"Iris.Bridge.dll", "Iris.Bridge.runtimeconfig.json", "Iris.Bridge.deps.json"})
                {
                    std::filesystem::copy_file(from / name, path / name);
                }
            }
            if (withContract)
            {
                std::filesystem::copy_file(from / "Fenrir.Scripting.dll", path / "Fenrir.Scripting.dll");
            }
        }
        ~BridgeCopy()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };

    // Every test starts from the same config, so a field added to ScriptingConfig later is not a warning each time.
    ScriptingConfig ConfigFor(const std::filesystem::path& scriptAssembly)
    {
        ScriptingConfig config;
        config.scriptAssembly = scriptAssembly;
        return config;
    }

    void RunsFramesWithTheFixture()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto messages = headless.Logger().Messages();
        Check(scripting.IsRunning(),
              "The runtime was not started: " + (messages.empty() ? std::string("no messages") : messages.back().text));
        Check(scripting.HasScripts(), "The fixture assembly did not load");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not run the requested frames");
        // Iris's own messages arrive through the engine logger, tagged as Iris's.
        const auto logged = headless.Logger().Messages();
        const bool fromIris = std::any_of(logged.begin(), logged.end(), [](const LoggedMessage& message) {
            return message.level == LogLevel::Info && message.text.rfind("[Iris] ", 0) == 0 &&
                   message.text.find("Fenrir.ScriptFixture") != std::string::npos;
        });
        Check(fromIris, "Iris's log messages did not reach the engine logger");
    }

    void MissingAssemblyIsLoggedAndTheAppRuns()
    {
        HeadlessApp headless;
        const auto missing = fixture.parent_path() / "does-not-exist.dll";
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(missing));
        Check(!scripting.HasScripts() && !scripting.IsRunning(), "A missing assembly started scripting");
        Check(headless.Logger().Contains(LogLevel::Error, "does-not-exist.dll"),
              "A missing assembly was not logged as an error naming it");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running without scripts");
    }

    void InvalidAssemblyIsLoggedAndTheAppRuns()
    {
        // Removed even when a check below throws.
        struct TempFile
        {
            std::filesystem::path path = std::filesystem::temp_directory_path() / "fenrir-not-an-assembly.dll";
            TempFile()
            {
                std::ofstream(path) << "this is not a .NET assembly";
            }
            ~TempFile()
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        } temp;
        const auto& invalid = temp.path;

        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(invalid));
        Check(!scripting.HasScripts() && !scripting.IsRunning(),
              "An invalid assembly loaded, or left the runtime running with nothing to run");
        Check(headless.Logger().Contains(LogLevel::Error, "could not be loaded"),
              "An invalid assembly was not logged as an error");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running after an invalid assembly");
    }

    void MissingBridgeIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noBridge(executableDirectory, /*withBridge=*/false, /*withContract=*/true);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noBridge.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        Check(!scripting.HasScripts() && !scripting.IsRunning(), "A missing bridge started scripting");
        Check(headless.Logger().Contains(LogLevel::Error, ".NET could not be started"),
              "A missing bridge was not logged as an error");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running without the bridge");
    }

    void MissingContractIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noContract(executableDirectory, /*withBridge=*/true, /*withContract=*/false);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noContract.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        Check(!scripting.HasScripts() && !scripting.IsRunning(), "A missing contract started scripting");
        Check(headless.Logger().Contains(LogLevel::Error, "Fenrir.Scripting.dll"),
              "A missing contract was not logged as an error naming it");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running without the contract");
    }

    void ASecondActiveInstallIsRefused()
    {
        HeadlessApp first;
        auto firstScripting = Scripting::Install(first.Get(), ConfigFor(fixture));
        Check(firstScripting.HasScripts(), "The first install did not load");
        HeadlessApp second;
        auto secondScripting = Scripting::Install(second.Get(), ConfigFor(fixture));
        Check(!secondScripting.HasScripts(), "A second active install loaded scripts");
        Check(second.Logger().Contains(LogLevel::Error, "already active"), "A second install was not explained");
        second.Run(2);
        Check(second.FramesRun() == 2, "The second app did not run");
        // The first is untouched, and a new install works once it is gone.
        Check(Call<int>(firstScripting, "Answer") == 42, "The first install stopped working");
    }

    void ScriptsLogThroughTheEngineLogger()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        Call<void>(scripting, "LogAll");
        const auto& logger = headless.Logger();
        Check(logger.Contains(LogLevel::Info, "hello from C#"), "Log.Info did not reach the engine logger");
        Check(logger.Contains(LogLevel::Warn, "careful now"), "Log.Warn did not reach the engine logger");
        Check(logger.Contains(LogLevel::Error, "boom"), "Log.Error did not reach the engine logger");
        // UTF-8 survives, and braces are text rather than a format string.
        Check(logger.Contains(LogLevel::Info, "pi \xCF\x80 {0} {} {name}"), "Non-ASCII text or braces were mangled");
        // There is no length limit, and null is an empty message.
        Check(logger.Contains(LogLevel::Info, std::string(2000, 'x')), "A long message was truncated");
        const auto messages = logger.Messages();
        Check(std::any_of(messages.begin(), messages.end(),
                          [](const LoggedMessage& message) { return message.level == LogLevel::Info && message.text.empty(); }),
              "A null message was not logged as an empty one");
    }

    void ScriptsReadTheEngineClock()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        bool sawDelta = false;
        bool matched = true;
        headless.Get().AddSequentialSystems(SchedulePriority::Update, {[&](App& app) {
                                                const float delta = Call<float>(scripting, "DeltaTime");
                                                const float tick = Call<float>(scripting, "TickRate");
                                                matched = matched && delta == static_cast<float>(app.GetTime().deltaTime) &&
                                                          tick == static_cast<float>(app.GetTime().tickRate);
                                                sawDelta = sawDelta || delta > 0.0F;
                                            }});
        headless.Run(Frames);
        Check(matched, "Time did not match the engine's clock");
        Check(sawDelta, "Time.DeltaTime was never positive");
    }

    void EntityValidityFollowsTheActiveScene()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto isValid = [&](std::uint32_t value) { return Call<bool>(scripting, "IsValid", value); };

        const auto first = entities.CreateEntity().GetId();
        Check(isValid(first), "A live entity was not valid");
        entities.DestroyEntity(first);
        Check(!isValid(first), "A destroyed entity was still valid");

        // The slot is reused with a new version, so the old value stays invalid and the new one is valid.
        const auto second = entities.CreateEntity().GetId();
        Check((second & 0xFFFFFU) == (first & 0xFFFFFU) && second != first,
              "The test did not reuse the entity's slot with a new version");
        Check(isValid(second), "The entity that reused the slot was not valid");
        Check(!isValid(first), "A stale entity became valid when its slot was reused");
        Check(!isValid(0xFFFFFFFFU), "The null entity was valid");
    }

    void RefWritesThroughGeneratedAccessorsIntoNativeMemory()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");

        // t.Position.X += 1 on a nested struct field, a plain assignment, a compound multiply and a flag: all land in
        // memory this test owns, with no ref at the call site.
        NativeTransform transform{{1, 2, 3}, 4, 1};
        Access<void>(scripting, "Nudge", AddressOf(&transform));
        Check(transform.Position.X == 2 && transform.Position.Y == 5 && transform.Position.Z == 3 &&
                  transform.Scale == 8 && transform.Flags == 5,
              "Writes through the generated accessors did not reach native memory");
        Check(Access<float>(scripting, "ReadSum", AddressOf(&transform)) == 18.0F,
              "Reads through the generated accessors did not see native memory");

        // Writes from C++ are seen too: it is the same memory.
        transform.Position.Z = 10;
        Check(Access<float>(scripting, "ReadSum", AddressOf(&transform)) == 25.0F,
              "A native write was not seen by the script");

        NativeNested nested{1.0};
        Check(Access<double>(scripting, "NudgeNested", AddressOf(&nested)) == 1.5 && nested.Weight == 1.5,
              "A nested component type got no accessors");
        NativeHidden hidden{4};
        Check(Access<std::int32_t>(scripting, "NudgeHidden", AddressOf(&hidden)) == 7 && hidden.Count == 7,
              "An internal component got no accessors");
        NativeClash clash{0, 0, 0};
        Check(Access<std::int32_t>(scripting, "NudgeClash", AddressOf(&clash)) == 24 && clash.Fine == 7 &&
                  clash.Value == 8 && clash.IsNull == 9,
              "Fields named like Ref<T> members were not reachable through Value");
        NativeSplit split{100, 200};
        Check(Access<std::int32_t>(scripting, "NudgeSplit", AddressOf(&split)) == 303 && split.A == 101 && split.B == 202,
              "A partial component did not get accessors for all of its fields");
        NativeReserved reserved{10, 20};
        Check(Access<std::int32_t>(scripting, "NudgeReserved", AddressOf(&reserved)) == 33 && reserved.Class == 11 &&
                  reserved.Event == 22,
              "Keyword-named fields were not exposed");
    }

    void ANullRefSaysSoInsteadOfCrashing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(Access<bool>(scripting, "NullRefIsNull"), "A null Ref was not reported as null");
        const auto message = Access<std::optional<std::string>>(scripting, "NullRefMessage");
        Check(message.has_value() && message->find("Pose") != std::string::npos,
              "Reading a null Ref did not throw a clear exception");
    }

    // ---- the native component registry

    void ATransformIsTheSameMemoryInCppAndInCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        auto& entities = headless.Get().GetActiveScene().GetEntityList();

        auto entity = entities.CreateEntity();
        entity.AddComponent<Transform>(Math::Vec3(1, 2, 3), Math::Quat(1, 0, 0, 0), Math::Vec3(1, 1, 1));
        const auto id = entity.GetId();

        // C++ wrote it; C# reads the same values (Rotation.W is the first float in glm's layout).
        Check(Registry<float>(scripting, "ReadPositionX", id) == 1.0F, "C# did not read the position C++ wrote (got " + std::to_string(Registry<float>(scripting, "ReadPositionX", id)) + ")");
        Check(Registry<float>(scripting, "ReadRotationW", id) == 1.0F, "C# did not read the rotation C++ wrote");
        Check(Registry<float>(scripting, "ReadScaleZ", id) == 1.0F, "C# did not read the scale C++ wrote");

        // C# writes through its Ref<Transform>; C++ sees it through GetComponent.
        Registry<void>(scripting, "Move", id);
        const auto& transform = entity.GetComponent<Transform>();
        Check(transform.pos.x == 2 && transform.pos.y == 2 && transform.pos.z == 3, "C# writes to the position were not seen");
        Check(transform.rot.w == 0.5F && transform.rot.x == 0 && transform.rot.y == 0 && transform.rot.z == 0,
              "C# writes to the rotation were not seen");
        Check(transform.scale.x == 3 && transform.scale.y == 3 && transform.scale.z == 3, "C# writes to the scale were not seen");

        // And the other way round, in the same memory.
        entity.GetComponent<Transform>().pos.x = 10;
        Check(Registry<float>(scripting, "ReadPositionX", id) == 10.0F, "C# did not see a later C++ write");

        // Has: live state, for an entity with and without the component (every engine entity starts with a Transform).
        auto bare = entities.CreateEntity();
        bare.RemoveComponent<Transform>();
        Check(Registry<bool>(scripting, "HasTransform", id) && !Registry<bool>(scripting, "HasTransform", bare.GetId()),
              "Has<Transform> was wrong");
        entities.DestroyEntity(id);
        Check(!Registry<bool>(scripting, "HasTransform", id), "Has<Transform> was true for a destroyed entity");
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
        Check(read.has_value() && *read == "h\xC3\xA9llo", "C# did not read the name C++ wrote");

        // C# to C++.
        Registry<void>(scripting, "WriteName", id, std::string_view("from C# \xCF\x80"));
        Check(std::string(entity.GetComponent<Name>().Get()) == "from C# \xCF\x80", "C++ did not read the name C# wrote");
        Check(Registry<std::optional<std::string>>(scripting, "ReadName", id).value() == "from C# \xCF\x80",
              "C# did not read back its own name");

        // A name longer than the buffer is cut between characters, and stays terminated.
        std::string tooLong;
        for (int index = 0; index < 200; ++index)
        {
            tooLong += "\xC3\xA9"; // 400 bytes
        }
        Registry<void>(scripting, "WriteName", id, std::string_view(tooLong));
        const std::string stored = entity.GetComponent<Name>().Get();
        Check(stored.size() == 254 && stored.size() % 2 == 0 && stored.substr(0, 4) == "\xC3\xA9\xC3\xA9",
              "A long name was not cut on a character boundary (it was " + std::to_string(stored.size()) + " bytes)");

        // An empty name clears the buffer.
        Registry<void>(scripting, "WriteName", id, std::string_view(""));
        Check(entity.GetComponent<Name>().IsEmpty(), "An empty name did not clear the buffer");
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
        Check(Registry<std::uint32_t>(scripting, "ParentOf", first.GetId()) == parent.GetId(), "The parent link was wrong");
        Check(Registry<std::uint32_t>(scripting, "ParentOf", second.GetId()) == parent.GetId(), "The second parent link was wrong");
        Check(Registry<std::uint32_t>(scripting, "FirstChildOf", parent.GetId()) == first.GetId(), "The first child link was wrong");
        Check(Registry<std::uint32_t>(scripting, "NextSiblingOf", first.GetId()) == second.GetId(), "The sibling link was wrong");
        Check(Registry<std::uint32_t>(scripting, "ParentOf", parent.GetId()) == NullEntity, "A missing parent was not a null link");
        Check(Registry<std::uint32_t>(scripting, "NextSiblingOf", second.GetId()) == NullEntity, "A missing sibling was not null");

        // It cannot be written from C#: no Ref<Relationship>, and assignments through ReadOnlyRef do not compile (see the
        // RefCompile-Relationship* tests). Native code can still change it, and C# sees that.
        parent.RemoveChild(first);
        Check(Registry<std::uint32_t>(scripting, "FirstChildOf", parent.GetId()) == second.GetId(),
              "C# did not see the hierarchy change");
    }

    void ACustomComponentRegisteredByTheTestWorksWithNoEngineChanges()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto entity = entities.CreateEntity();
        const auto id = entity.GetId();

        // Looking Health up before it is registered finds nothing...
        const auto early = Registry<std::optional<std::string>>(scripting, "HealthLookup", id);
        Check(early.has_value() && early->find("Test.Health") != std::string::npos, "An unregistered Health was not explained");

        // Before it is registered, the engine knows nothing of it.
        const auto unregistered = Registry<std::optional<std::string>>(scripting, "NotRegistered", id);
        Check(unregistered.has_value() && unregistered->find("Test.Nothing") != std::string::npos,
              "A component nobody registered was not explained");

        const auto registered = scripting.RegisterComponent<Health>("Test.Health");
        Check(registered.Succeeded() && scripting.HasScripts(), "A matching component was not registered: " + registered.diagnostic);
        Check(!scripting.RegisterComponent<Health>("Test.Health").Succeeded(), "A duplicate name was accepted");

        // ...but registering it later is seen: the earlier "not registered" is not remembered past a change. It is now
        // found, and the entity simply does not have it yet.
        const auto late = Registry<std::optional<std::string>>(scripting, "HealthLookup", id);
        Check(late.has_value() && late->find("has no Health") != std::string::npos,
              "A component registered after a failed lookup was still reported as unregistered: " + late.value_or("<no exception>"));
        Check(!scripting.RegisterComponent<Health>("").Succeeded(), "An empty name was accepted");

        Check(!Registry<bool>(scripting, "HasHealth", id), "Has<Health> was true before it was added");
        entity.AddComponent<Health>();
        entity.GetComponent<Health>() = Health{20, 30};
        Check(Registry<bool>(scripting, "HasHealth", id), "Has<Health> was false after it was added");
        Check(Registry<std::int32_t>(scripting, "HealthSum", id) == 55, "C# did not read the custom component");
        Check(entity.GetComponent<Health>().Current == 25, "C# writes to the custom component were not seen");
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
        Check(missing.has_value() && missing->find("has no Transform") != std::string::npos, "A missing component was not explained (got " + missing.value_or("<no exception>") + ")");
        const auto dead = Registry<std::optional<std::string>>(scripting, "MissingComponent", goneId);
        Check(dead.has_value() && dead->find("not alive") != std::string::npos, "A dead entity was not explained");
    }

    void AMismatchedComponentFailsTheLoadAndNamesTheField()
    {
        HeadlessApp headless;
        ScriptingConfig config = ConfigFor(fixture);
        config.components.push_back(MakeComponent<BadHealth>("Test.BadHealth"));
        auto scripting = Scripting::Install(headless.Get(), config);

        // All or nothing: the assembly is not left loaded with a component that does not match.
        Check(!scripting.HasScripts() && !scripting.IsRunning(), "A mismatched component left scripts loaded");
        Check(headless.Logger().Contains(LogLevel::Error, "Test.BadHealth") && headless.Logger().Contains(LogLevel::Error, "'Current'") &&
                  headless.Logger().Contains(LogLevel::Error, "'Max'"),
              "The mismatch did not name the component and the field");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not keep running after a failed load");

        // The engine's own components still load once the bad one is gone.
        HeadlessApp again;
        auto fine = Scripting::Install(again.Get(), ConfigFor(fixture));
        Check(fine.HasScripts(), "A later install without the bad component did not load");
    }

    void AScriptCannotClaimANativeComponentWithAStructOfItsOwn()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity();

        // The fixture declares a writable component that names the hierarchy ("Fenrir.Relationship"). The engine only hands
        // out a native component to the C# type it validated, so the claim is refused.
        const auto refused = Registry<std::optional<std::string>>(scripting, "FakeHierarchy", entity.GetId());
        Check(refused.has_value() && refused->find("different C# type") != std::string::npos,
              "A script's own struct got a native component: " + refused.value_or("<no exception>"));
    }

    void AWrongFieldTypeIsCaughtToo()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto registered = scripting.RegisterComponent<FloatHealth>("Test.FloatHealth");
        Check(registered.status == Iris::CallStatus::InvalidArgument && registered.diagnostic.find("'Current'") != std::string::npos,
              "A field of the wrong type was accepted or undiagnosed: " + registered.diagnostic);
        Check(!scripting.HasScripts(), "A mismatched registration left the script assembly loaded");
    }

    void AComponentRegisteredLaterIsCheckedAtOnce()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");
        const auto registered = scripting.RegisterComponent<BadHealth>("Test.BadHealth");
        Check(registered.status == Iris::CallStatus::InvalidArgument && registered.diagnostic.find("'Current'") != std::string::npos &&
                  registered.diagnostic.find("Test.BadHealth") != std::string::npos,
              "A mismatched registration was accepted or undiagnosed: " + registered.diagnostic);
        Check(!scripting.HasScripts(), "A mismatched registration left the script assembly loaded");
        Check(headless.Logger().Contains(LogLevel::Error, "'Current'"), "The mismatch was not logged");
    }

    // ---- components defined in C#

    constexpr const char* VelocityName = "Fenrir.ScriptFixture.Velocity";
    constexpr const char* CounterName = "Fenrir.ScriptFixture.Counter";
    constexpr const char* AlignedName = "Fenrir.ScriptFixture.Aligned";
    constexpr const char* TaggedName = "Fenrir.ScriptFixture.Tagged";
    constexpr const char* ScriptedType = "Fenrir.ScriptFixture.Scripted";

    template <typename Result, typename... Args>
    Result Scripted(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ScriptedType, scripting, method, arguments...);
    }

    std::size_t PoolCount(EntityList& entities)
    {
        std::size_t count = 0;
        for (auto&& [id, storage] : entities.GetRegistry().storage())
        {
            (void)id;
            (void)storage;
            ++count;
        }
        return count;
    }

    void AScriptAssemblyReportsTheComponentsItDefines()
    {
        HeadlessApp headless;
        auto& scene = headless.Get().GetActiveScene().GetEntityList();
        const std::size_t poolsBefore = PoolCount(scene);
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "The fixture did not load");

        // Registering components creates no storage, and neither does asking about them: a pool appears when a component is
        // first added in a scene. (So "a failed load leaves no pools" is a real property, not an accident of timing.)
        Check(PoolCount(scene) == poolsBefore, "Loading a script assembly created storage");
        const auto* velocity = scripting.FindComponent(VelocityName);
        const auto probe = scene.CreateEntity().GetId();
        const std::size_t poolsWithEntity = PoolCount(scene);
        Check(!velocity->has(scene, probe) && velocity->get(scene, probe) == nullptr && velocity->remove(scene, probe),
              "Reading or removing an absent script component misbehaved");
        Check(PoolCount(scene) == poolsWithEntity, "Asking about a script component created storage");
        Check(velocity->add(scene, probe) && PoolCount(scene) == poolsWithEntity + 1, "Adding a script component did not create its pool");

        const auto expect = [&](const char* name, std::size_t size, std::size_t alignment) {
            const auto* registration = scripting.FindComponent(name);
            Check(registration != nullptr, std::string("The script component ") + name + " was not registered");
            Check(registration->scriptDefined && !registration->readOnly && registration->typeName == name,
                  std::string(name) + " was not described as a script component");
            Check(registration->size == size && registration->alignment == alignment,
                  std::string(name) + " has the wrong size or alignment: " + std::to_string(registration->size) + " / " +
                      std::to_string(registration->alignment));
        };
        expect(VelocityName, 12, 4);
        expect(CounterName, 16, 8);
        expect(AlignedName, 32, 16);
        expect(TaggedName, 1, 1);

        // Native components are not script-defined, and a [NativeComponent] struct in the script is not reported as one.
        Check(!scripting.FindComponent("Fenrir.Transform")->scriptDefined, "A native component was described as script-defined");
        Check(scripting.FindComponent("Fenrir.ScriptFixture.Health") == nullptr,
              "A [NativeComponent] struct was reported as a script-defined component");
    }

    void AScriptComponentIsSharedWithCSharpAndRemovedWithItsEntity()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);
        Check(velocity != nullptr, "Velocity was not registered");

        auto first = entities.CreateEntity();
        auto second = entities.CreateEntity();
        auto third = entities.CreateEntity();
        Check(!velocity->has(entities, first.GetId()), "A component was there before it was added");
        for (auto* entity : {&first, &second, &third})
        {
            Check(velocity->add(entities, entity->GetId()), "A component was not added");
        }
        Check(velocity->add(entities, first.GetId()), "Adding twice failed"); // Harmless, and keeps the one it has.

        // A new component is all zero bytes, which is a C# default.
        Check(Scripted<float>(scripting, "SpeedX", first.GetId()) == 0.0F, "A new script component was not zeroed");

        // C# writes to it; native code sees the same bytes.
        Scripted<void>(scripting, "Accelerate", first.GetId());
        Scripted<void>(scripting, "Accelerate", first.GetId());
        Scripted<void>(scripting, "Accelerate", third.GetId());
        const auto* values = static_cast<const float*>(velocity->get(entities, first.GetId()));
        Check(values != nullptr && values[0] == 2 && values[1] == 4 && values[2] == 6, "C# writes were not in the engine's storage");
        Check(Scripted<float>(scripting, "SpeedX", third.GetId()) == 1.0F, "A second entity's component was wrong");
        Check(Scripted<float>(scripting, "SpeedX", second.GetId()) == 0.0F, "A write reached the wrong entity's component");

        // Native code writes it too, and C# reads that.
        static_cast<float*>(velocity->get(entities, second.GetId()))[0] = 42;
        Check(Scripted<float>(scripting, "SpeedX", second.GetId()) == 42.0F, "C# did not read a native write");

        // The pool holds one element per entity that has the component.
        Check(velocity->pool(entities)->size() == 3, "The pool did not hold three elements");

        // Destroying an entity removes its element, and the others keep their values.
        entities.DestroyEntity(second.GetId());
        Check(velocity->pool(entities)->size() == 2, "Destroying an entity left its component in the pool");
        Check(!velocity->has(entities, second.GetId()), "A destroyed entity still had the component");
        Check(Scripted<float>(scripting, "SpeedX", first.GetId()) == 2.0F && Scripted<float>(scripting, "SpeedX", third.GetId()) == 1.0F,
              "Removing an element disturbed the others");

        // The slot is reused by a new entity, which does not inherit the old component.
        const auto reused = entities.CreateEntity().GetId();
        Check(!velocity->has(entities, reused), "A new entity inherited a destroyed entity's component");
        Check(!Scripted<bool>(scripting, "HasVelocity", reused), "C# saw a component on an entity that has none");
        Check(Scripted<bool>(scripting, "HasVelocity", first.GetId()), "C# did not see a component that is there");

        // Removing one explicitly, and again (harmless).
        Check(velocity->remove(entities, first.GetId()) && !velocity->has(entities, first.GetId()), "A component was not removed");
        Check(velocity->remove(entities, first.GetId()), "Removing twice failed");
        Check(velocity->pool(entities)->size() == 1, "The pool did not shrink");
    }

    void ARuntimeViewOverNativeAndScriptPoolsIteratesTheRightEntities()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* transform = scripting.FindComponent("Fenrir.Transform");
        const auto* velocity = scripting.FindComponent(VelocityName);
        const auto* counter = scripting.FindComponent(CounterName);

        std::vector<std::uint32_t> ids;
        for (int index = 0; index < 6; ++index)
        {
            ids.push_back(entities.CreateEntity().GetId());
        }
        // Every entity has a Transform (the engine adds it). Velocity on 0, 2, 4. Counter on 2, 3, 4, 5. Without a
        // Transform: entity 4.
        for (const int index : {0, 2, 4})
        {
            Check(velocity->add(entities, ids[static_cast<std::size_t>(index)]), "Velocity was not added");
        }
        for (const int index : {2, 3, 4, 5})
        {
            Check(counter->add(entities, ids[static_cast<std::size_t>(index)]), "Counter was not added");
        }
        Check(transform->remove(entities, ids[4]), "Transform was not removed");

        const auto iterate = [&](std::initializer_list<const ComponentRegistration*> registrations) {
            entt::runtime_view view;
            for (const auto* registration : registrations)
            {
                view.iterate(*registration->pool(entities));
            }
            std::set<std::uint32_t> found;
            for (const auto entity : view)
            {
                found.insert(static_cast<std::uint32_t>(entity));
            }
            return found;
        };
        using Ids = std::set<std::uint32_t>;
        Check(iterate({transform, velocity}) == Ids{ids[0], ids[2]}, "Native + script view was wrong (Transform and Velocity)");
        Check(iterate({velocity, counter}) == Ids{ids[2], ids[4]}, "Script + script view was wrong (Velocity and Counter)");
        Check(iterate({transform, velocity, counter}) == Ids{ids[2]}, "A three-pool view was wrong");
        Check(iterate({counter}) == Ids{ids[2], ids[3], ids[4], ids[5]}, "A single script pool iterated the wrong entities");

        // Destroying an entity drops it from every pool, native and script.
        entities.DestroyEntity(ids[2]);
        Check(iterate({transform, velocity}) == Ids{ids[0]}, "A destroyed entity stayed in a view");
        Check(iterate({counter}) == Ids{ids[3], ids[4], ids[5]}, "A destroyed entity stayed in a script pool");
    }

    void ScriptComponentStorageHonoursAlignmentAndNeverMovesElements()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* aligned = scripting.FindComponent(AlignedName);
        Check(aligned != nullptr, "Aligned was not registered");

        // More elements than one page holds (a page is 256), each on a 16-byte boundary.
        std::vector<std::uint32_t> ids;
        for (int index = 0; index < 700; ++index)
        {
            ids.push_back(entities.CreateEntity().GetId());
            Check(aligned->add(entities, ids.back()), "Aligned was not added");
            Check(reinterpret_cast<std::uintptr_t>(aligned->get(entities, ids.back())) % 16 == 0,
                  "An element was not 16-byte aligned");
        }

        // Growing the storage does not move what is already in it.
        void* first = aligned->get(entities, ids.front());
        void* middle = aligned->get(entities, ids[400]);
        for (int index = 0; index < 3000; ++index)
        {
            const auto id = entities.CreateEntity().GetId();
            Check(aligned->add(entities, id), "Aligned was not added while growing");
        }
        Check(aligned->get(entities, ids.front()) == first && aligned->get(entities, ids[400]) == middle,
              "Growing the storage moved an existing element");
        Check(aligned->pool(entities)->size() == 3700, "The pool did not hold every element");

        // C# reads and writes a Vector128 in it.
        Scripted<void>(scripting, "FillAligned", ids[400]);
        const auto* lanes = static_cast<const float*>(aligned->get(entities, ids[400]));
        Check(lanes[0] == 1 && lanes[3] == 4 && static_cast<const std::uint8_t*>(aligned->get(entities, ids[400]))[16] == 7,
              "C# writes to an aligned component were not in the storage");
        Check(Scripted<float>(scripting, "AlignedLane", ids[400], 2) == 3.0F, "C# did not read a lane back");

        // A tag with no data is still a component.
        const auto* tagged = scripting.FindComponent(TaggedName);
        Check(tagged->add(entities, ids[1]) && Scripted<bool>(scripting, "HasTagged", ids[1]) && !Scripted<bool>(scripting, "HasTagged", ids[2]),
              "An empty component did not work as a tag");
    }

    void TheByteStorageRefusesToChangeShapeWhileInUse()
    {
        entt::registry registry;
        auto& storage = registry.storage<Detail::ScriptComponentTag>(entt::hashed_string{"shape"});
        const auto entity = registry.create();

        Check(!storage.IsConfigured() && storage.Emplace(entity) == nullptr, "An unconfigured storage accepted an element");
        Check(!storage.Configure(0, 4) && !storage.Configure(8, 0) && !storage.Configure(8, 3) && !storage.Configure(7, 4),
              "An impossible size or alignment was accepted");
        Check(storage.Configure(8, 4) && storage.Configure(8, 4), "A valid configuration, or repeating it, failed");

        auto* element = static_cast<std::uint8_t*>(storage.Emplace(entity));
        Check(element != nullptr && std::all_of(element, element + 8, [](std::uint8_t byte) { return byte == 0; }),
              "A new element was not zeroed");
        Check(storage.Emplace(entity) == element, "Adding an entity twice gave it a second element");
        Check(!storage.Configure(16, 8), "The shape changed while an element was stored");
        Check(storage.Find(entity) == element && storage.ElementSize() == 8, "A refused change disturbed the storage");

        // Empty again, it can take a new shape.
        registry.destroy(entity);
        Check(storage.size() == 0 && storage.Configure(16, 8) && storage.ElementSize() == 16,
              "An emptied storage did not accept a new shape");
        const auto next = registry.create();
        Check(reinterpret_cast<std::uintptr_t>(storage.Emplace(next)) % 8 == 0, "The new shape was not honoured");
    }

    void AFailedLoadLeavesNoComponentsAndNoPools()
    {
        HeadlessApp headless;
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const std::size_t poolsBefore = PoolCount(entities);

        // The script assembly defines components and loads fine, then a native mapping fails: the load is rolled back.
        ScriptingConfig config = ConfigFor(fixture);
        config.components.push_back(MakeComponent<BadHealth>("Test.BadHealth"));
        auto scripting = Scripting::Install(headless.Get(), config);
        Check(!scripting.HasScripts(), "A mismatched component left scripts loaded");
        Check(scripting.FindComponent(VelocityName) == nullptr && scripting.FindComponent(CounterName) == nullptr &&
                  scripting.FindComponent(AlignedName) == nullptr && scripting.FindComponent(TaggedName) == nullptr,
              "A failed load left script components registered");
        Check(scripting.FindComponent("Fenrir.Transform") != nullptr && scripting.FindComponent("Test.BadHealth") != nullptr,
              "A failed load disturbed the components that were registered before it");
        Check(PoolCount(entities) == poolsBefore, "A failed load left storage behind");

        // And a clean load afterwards registers them, with nothing carried over from the failure.
        HeadlessApp again;
        auto fine = Scripting::Install(again.Get(), ConfigFor(fixture));
        Check(fine.FindComponent(VelocityName) != nullptr, "A later clean load did not register the script components");
    }

    void AScriptComponentCannotTakeANativeComponentsName()
    {
        HeadlessApp headless;
        ScriptingConfig config = ConfigFor(fixture);
        // A native component already holds the name the script's Velocity would use.
        config.components.push_back(MakeComponent<Health>(VelocityName));
        auto scripting = Scripting::Install(headless.Get(), config);
        Check(!scripting.HasScripts(), "A script component took a native component's name");
        Check(headless.Logger().Contains(LogLevel::Error, "already has a component of that name") &&
                  headless.Logger().Contains(LogLevel::Error, VelocityName),
              "The name clash was not explained");
        Check(scripting.FindComponent(CounterName) == nullptr, "The clash left part of the script's components registered");
    }

    void TheContractIsOneSharedCopyAcrossGenerations()
    {
        int firstIdentity = 0;
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            Check(Call<bool>(scripting, "ContractIsShared"),
                  "The contract was not the host's shared copy in the default load context");
            firstIdentity = Call<int>(scripting, "ContractIdentity");
            // And it is the host's deployed file (beside the executable), not a copy a script brought along.
            const auto reported = Call<std::optional<std::string>>(scripting, "ContractLocation");
            Check(reported.has_value(), "The contract reported no location");
            const auto& location = *reported;
            Check(std::filesystem::equivalent(location, executableDirectory / "Fenrir.Scripting.dll"),
                  "The contract was not loaded from the host's directory: " + location);
        }
        // A fresh install loads the script assembly again; the contract it sees is the very same assembly.
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(Call<bool>(scripting, "ContractIsShared"), "The contract was not shared on the second install");
        Check(Call<int>(scripting, "ContractIdentity") == firstIdentity,
              "The contract changed identity between generations");
    }

    void TheBuildDeploysTheBridgeNextToTheExecutable()
    {
        // iris_deploy puts these beside the executable, which is where Scripting looks by default.
        for (const char* name : {"Iris.Bridge.dll", "Iris.Bridge.runtimeconfig.json", "Iris.Bridge.deps.json"})
        {
            Check(std::filesystem::exists(executableDirectory / name), std::string(name) + " is not next to the executable");
        }
        Check(!std::filesystem::is_empty(executableDirectory), "The executable directory is empty");
    }

    void NoAssemblyConfiguredDoesNothing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ScriptingConfig{});
        Check(!scripting.IsRunning() && !scripting.HasScripts(), "An empty config started scripting");
        headless.Run(Frames);
        Check(headless.FramesRun() == Frames, "The app did not run");
    }

    void ShutdownReleasesEverythingWhenTheAppFinishes()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        Check(scripting.IsRunning() && scripting.HasScripts(), "The fixture did not load");
        headless.Run(Frames);
        // The App's Exit phase has released the script assembly and the runtime host: nothing of Iris remains
        // before the owner is even destroyed.
        Check(!scripting.IsRunning() && !scripting.HasScripts(), "Iris still held the runtime after the app finished");
        scripting.Shutdown(); // Idempotent.
        Check(!scripting.IsRunning(), "Shutdown twice restarted something");
    }

    void DestroyingTheOwnerWithoutRunningReleasesEverything()
    {
        // Another install in the same process works after the previous one was released.
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            Check(scripting.HasScripts(), "The first install did not load");
        }
        HeadlessApp second;
        auto scripting = Scripting::Install(second.Get(), ConfigFor(fixture));
        Check(scripting.HasScripts(), "A second install after a destroyed one did not load");
        second.Run(2);
        Check(second.FramesRun() == 2, "The second app did not run");
    }
} // namespace

int main(int argc, char* argv[])
{
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() != 2)
    {
        std::cerr << "Usage: FenrirScriptingTests Fenrir.ScriptFixture.dll\n";
        return EXIT_FAILURE;
    }
    fixture = std::filesystem::absolute(arguments[1]);
    executableDirectory = std::filesystem::absolute(arguments[0]).parent_path();
    return RunCases({
        {"the fixture loads, frames run and Iris's messages reach the engine logger", RunsFramesWithTheFixture},
        {"a missing script assembly is logged and the app keeps running", MissingAssemblyIsLoggedAndTheAppRuns},
        {"an invalid script assembly is logged and the app keeps running", InvalidAssemblyIsLoggedAndTheAppRuns},
        {"a missing bridge is logged and the app keeps running", MissingBridgeIsLoggedAndTheAppRuns},
        {"a missing contract is logged and the app keeps running", MissingContractIsLoggedAndTheAppRuns},
        {"a second active install is refused and explained", ASecondActiveInstallIsRefused},
        {"scripts log through the engine logger at every level", ScriptsLogThroughTheEngineLogger},
        {"scripts read the engine clock", ScriptsReadTheEngineClock},
        {"entity validity follows the active scene, including slot reuse", EntityValidityFollowsTheActiveScene},
        {"the contract is one shared copy across generations", TheContractIsOneSharedCopyAcrossGenerations},
        {"Ref<T> writes through generated accessors into native memory", RefWritesThroughGeneratedAccessorsIntoNativeMemory},
        {"a null Ref says so instead of crashing", ANullRefSaysSoInsteadOfCrashing},
        {"a Transform is the same memory in C++ and C#", ATransformIsTheSameMemoryInCppAndInCSharp},
        {"a Name round-trips both ways, and long text is cut between characters", ANameRoundTripsBothWays},
        {"the hierarchy is readable from C# but not writable", TheHierarchyIsReadableButNotWritable},
        {"a component registered by the test works with no engine changes",
         ACustomComponentRegisteredByTheTestWorksWithNoEngineChanges},
        {"lookup failures say what went wrong", LookupFailuresSayWhatWentWrong},
        {"a mismatched component fails the load and names the field", AMismatchedComponentFailsTheLoadAndNamesTheField},
        {"a component registered later is checked at once", AComponentRegisteredLaterIsCheckedAtOnce},
        {"a script assembly reports the components it defines, with size and alignment", AScriptAssemblyReportsTheComponentsItDefines},
        {"a script component is shared with C# and removed with its entity", AScriptComponentIsSharedWithCSharpAndRemovedWithItsEntity},
        {"a runtime view over native and script pools iterates the right entities",
         ARuntimeViewOverNativeAndScriptPoolsIteratesTheRightEntities},
        {"script component storage honours alignment and never moves elements", ScriptComponentStorageHonoursAlignmentAndNeverMovesElements},
        {"the byte storage refuses to change shape while in use", TheByteStorageRefusesToChangeShapeWhileInUse},
        {"a failed load leaves no components and no pools", AFailedLoadLeavesNoComponentsAndNoPools},
        {"a script component cannot take a native component's name", AScriptComponentCannotTakeANativeComponentsName},
        {"a wrong field type is caught too", AWrongFieldTypeIsCaughtToo},
        {"a script cannot claim a native component with a struct of its own", AScriptCannotClaimANativeComponentWithAStructOfItsOwn},
        {"the build deploys the bridge next to the executable", TheBuildDeploysTheBridgeNextToTheExecutable},
        {"no configured assembly does nothing and never starts .NET", NoAssemblyConfiguredDoesNothing},
        {"everything Iris owns is released when the app finishes", ShutdownReleasesEverythingWhenTheAppFinishes},
        {"destroying the owner releases everything, and a later install works",
         DestroyingTheOwnerWithoutRunningReleasesEverything},
    });
}
