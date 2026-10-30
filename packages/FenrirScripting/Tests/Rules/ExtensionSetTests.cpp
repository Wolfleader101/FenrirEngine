// The extension set's rules, with no .NET: every native function is named Namespace.Type.Method; no name is declared
// twice, by one extension or by two; a native taking an extension state is listed by an extension carrying a state of
// its type; every rule is checked before anything is registered; every native is registered in extension order with its
// extension's state; a refused registration fails the set and names the function; the states outlive the extensions
// and expire with the set; the shared assemblies come back in extension order; a stage is declared once, with a name,
// by an extension that places and removes its groups. What Iris checks when the contracts are shared (an unbound C#
// field, a mismatched signature) is pinned by AdmissionTests through the real runtime.
#include <catch2/catch_test_macros.hpp>

#include "FakeNativeRegistrar.hpp"

#include "ExtensionSet.hpp"

#include "FenrirScripting/NativeFunctionMake.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using Fenrir::CallContext;
    using Fenrir::NativeFunction;
    using Fenrir::ScriptingExtension;
    using Fenrir::Detail::ExtensionSet;
    using Names = std::vector<std::string>;
    using Paths = std::vector<std::filesystem::path>;

    // Two extension states of different types, and the natives that take them or none.
    struct Tally
    {
        int count = 0;
    };

    struct Other
    {
        int count = 0;
    };

    int Stateless(const CallContext&)
    {
        return 1;
    }

    int WithTally(const CallContext&, Tally& tally)
    {
        return ++tally.count;
    }

    int WithOther(const CallContext&, Other& other)
    {
        return ++other.count;
    }

    ScriptingExtension Extension(const char* name, std::vector<NativeFunction> functions, Paths sharedAssemblies = {})
    {
        ScriptingExtension extension;
        extension.name = name;
        extension.functions = std::move(functions);
        extension.sharedAssemblies = std::move(sharedAssemblies);
        return extension;
    }

    // An extension carrying a Tally, with one stateful and one stateless native.
    ScriptingExtension Gadgets(std::shared_ptr<Tally> tally, const char* name = "gadgets")
    {
        auto extension = Extension(name,
                                   {NativeFunction::Make<&WithTally>("Rules.Gadgets.Tally"),
                                    NativeFunction::Make<&Stateless>("Rules.Gadgets.Answer")},
                                   {"Rules.Gadgets.dll"});
        extension.SetState(std::move(tally));
        return extension;
    }

    bool Contains(const std::string& text, const char* part)
    {
        return text.find(part) != std::string::npos;
    }
} // namespace

TEST_CASE("a native function not named Namespace.Type.Method fails the set, naming it and its extension, and nothing "
          "is registered",
          "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Gadgets(std::make_shared<Tally>()));
    extensions.push_back(Extension("misnamed", {NativeFunction::Make<&Stateless>("Answer")}));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("A native with no type part should be refused as InvalidArgument");
    REQUIRE((!installed.Succeeded() && installed.status == Iris::CallStatus::InvalidArgument));
    INFO("The failure did not name the function, its extension and the rule: " + installed.diagnostic);
    REQUIRE((Contains(installed.diagnostic, "'Answer'") && Contains(installed.diagnostic, "'misnamed'") &&
             Contains(installed.diagnostic, "is not named Namespace.Type.Method")));
    INFO("Nothing should be registered when a rule fails, the sound extension's natives included");
    REQUIRE(registrar.registered.empty());
}

TEST_CASE("a name declared twice by one extension fails the set, naming the extension", "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Extension("twice", {NativeFunction::Make<&Stateless>("Rules.Twice.Answer"),
                                             NativeFunction::Make<&Stateless>("Rules.Twice.Answer")}));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("A name declared twice should be refused");
    REQUIRE(!installed.Succeeded());
    INFO("The failure did not say the one extension declared it twice: " + installed.diagnostic);
    REQUIRE(installed.diagnostic == "the native function 'Rules.Twice.Answer' is declared twice by extension 'twice'");
    REQUIRE(registrar.registered.empty());
}

TEST_CASE("a name declared by two extensions fails the set, naming both", "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Gadgets(std::make_shared<Tally>()));
    extensions.push_back(Extension("copying", {NativeFunction::Make<&Stateless>("Rules.Gadgets.Answer")}));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("A name two extensions declare should be refused");
    REQUIRE(!installed.Succeeded());
    INFO("The failure did not name the function and both extensions: " + installed.diagnostic);
    REQUIRE(installed.diagnostic == "the native function 'Rules.Gadgets.Answer' is declared by both extension "
                                    "'gadgets' and extension 'copying'");
    REQUIRE(registrar.registered.empty());
}

TEST_CASE("a stateful native in an extension carrying no state fails the set, saying it carries none",
          "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Extension("stateless", {NativeFunction::Make<&WithTally>("Rules.Gadgets.Tally")}));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("A stateful native in a stateless extension should be refused");
    REQUIRE(!installed.Succeeded());
    INFO("The failure did not name the function, its state type and say the extension carries none: " +
         installed.diagnostic);
    REQUIRE((Contains(installed.diagnostic, "'Rules.Gadgets.Tally'") && Contains(installed.diagnostic, "Tally") &&
             Contains(installed.diagnostic, "carries none")));
    REQUIRE(registrar.registered.empty());
}

TEST_CASE("a stateful native in an extension carrying a state of another type fails the set, naming both types",
          "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    auto extension = Extension("other", {NativeFunction::Make<&WithTally>("Rules.Gadgets.Tally")});
    extension.SetState(std::make_shared<Other>());
    extensions.push_back(std::move(extension));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("A native taking another type's state should be refused");
    REQUIRE(!installed.Succeeded());
    // The types are spelled by type_index::name(), which GCC and Clang mangle, so only the types' own names are looked
    // for.
    INFO("The failure did not name the function and both state types: " + installed.diagnostic);
    REQUIRE((Contains(installed.diagnostic, "'Rules.Gadgets.Tally'") && Contains(installed.diagnostic, "Tally") &&
             Contains(installed.diagnostic, "carries one of type") && Contains(installed.diagnostic, "Other")));
    REQUIRE(registrar.registered.empty());
}

TEST_CASE("a stateless native in a stateful extension is registered with no state", "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    const auto tally = std::make_shared<Tally>();
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Gadgets(tally));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("A stateless native beside a stateful one should be fine: " + installed.diagnostic);
    REQUIRE(installed.Succeeded());
    REQUIRE(registrar.registered.size() == 2);
    INFO("The stateful native should receive the extension's state and the stateless one none");
    REQUIRE((registrar.registered[0].state == tally.get() && registrar.registered[1].state == nullptr));
}

TEST_CASE("every native of every extension is registered in extension order, under its binding and native names, with "
          "its own extension's state",
          "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    const auto first = std::make_shared<Tally>();
    const auto second = std::make_shared<Other>();
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Gadgets(first));
    auto widgets = Extension("widgets", {NativeFunction::Make<&WithOther>("Rules.Widgets+Inner.Count")});
    widgets.SetState(second);
    extensions.push_back(std::move(widgets));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("Two sound extensions should install: " + installed.diagnostic);
    REQUIRE(installed.Succeeded());
    INFO("The natives were not registered in extension order under their binding names");
    REQUIRE(registrar.BindingNames() == Names{"Rules.Gadgets.__native_Tally", "Rules.Gadgets.__native_Answer",
                                              "Rules.Widgets+Inner.__native_Count"});
    INFO("The native names (the type-and-method part) were not what the call context will carry");
    REQUIRE((registrar.registered[0].nativeName == "Gadgets.Tally" &&
             registrar.registered[2].nativeName == "Widgets+Inner.Count"));
    INFO("Each stateful native should receive its own extension's state");
    REQUIRE((registrar.registered[0].state == first.get() && registrar.registered[2].state == second.get()));
}

TEST_CASE("a refused registration fails the set with the function, the extension and the registrar's diagnostic, and "
          "what was registered before it stays registered",
          "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    registrar.refuse = "Rules.Widgets.__native_Second";
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Gadgets(std::make_shared<Tally>()));
    extensions.push_back(Extension("widgets", {NativeFunction::Make<&Stateless>("Rules.Widgets.First"),
                                               NativeFunction::Make<&Stateless>("Rules.Widgets.Second"),
                                               NativeFunction::Make<&Stateless>("Rules.Widgets.Third")}));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("A refused registration should fail the set with the registrar's status");
    REQUIRE((!installed.Succeeded() && installed.status == Iris::CallStatus::InvalidArgument));
    INFO("The failure did not name the function, the extension and carry the registrar's diagnostic: " +
         installed.diagnostic);
    REQUIRE(installed.diagnostic == "the native function 'Rules.Widgets.Second' of extension 'widgets' could not be "
                                    "registered: the fake registrar refused Rules.Widgets.__native_Second");
    INFO("Registration stops at the refusal; what came before it was asked of the registrar and stays");
    REQUIRE(registrar.BindingNames() == Names{"Rules.Gadgets.__native_Tally", "Rules.Gadgets.__native_Answer",
                                              "Rules.Widgets.__native_First", "Rules.Widgets.__native_Second"});
}

TEST_CASE("every rule is checked before anything is registered: a duplicate in the second extension registers nothing "
          "of the first",
          "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Gadgets(std::make_shared<Tally>()));
    extensions.push_back(Extension("copying", {NativeFunction::Make<&Stateless>("Rules.Copying.Answer"),
                                               NativeFunction::Make<&Stateless>("Rules.Gadgets.Answer")}));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("The duplicate should be refused");
    REQUIRE(!installed.Succeeded());
    INFO("The first extension's natives were registered before the second was checked");
    REQUIRE(registrar.registered.empty());
}

TEST_CASE("the extension states outlive the extensions handed in and expire with the set", "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::weak_ptr<Tally> watched;
    std::optional<ExtensionSet> set;
    {
        auto tally = std::make_shared<Tally>();
        watched = tally;
        std::vector<ScriptingExtension> extensions;
        extensions.push_back(Gadgets(std::move(tally)));
        auto installed = ExtensionSet::Install(std::move(extensions), registrar);
        REQUIRE(installed.Succeeded());
        set = std::move(*installed);
    }

    INFO("The state should be alive while the set is, with nothing else holding it");
    REQUIRE((!watched.expired() && watched.use_count() == 1));

    set.reset();

    INFO("The state should expire with the set");
    REQUIRE(watched.expired());
}

TEST_CASE("the shared assemblies come back in extension order", "[extension-set]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Extension("built-in", {}, {"Fenrir.Math.dll", "Fenrir.Scripting.dll"}));
    extensions.push_back(Gadgets(std::make_shared<Tally>()));
    extensions.push_back(Extension("silent", {}));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("Extensions with no natives should install: " + installed.diagnostic);
    REQUIRE(installed.Succeeded());
    INFO("The shared assemblies were not every extension's, in extension order");
    REQUIRE((*installed).SharedAssemblies() == Paths{"Fenrir.Math.dll", "Fenrir.Scripting.dll", "Rules.Gadgets.dll"});
}

namespace
{
    // An extension hosting `stages`, whose placing and removing do nothing (or are left out, when `hooks` is false).
    ScriptingExtension Stages(const char* name, std::vector<std::string> stages, bool hooks = true)
    {
        auto extension = Extension(name, {});
        extension.stages = std::move(stages);
        if (hooks)
        {
            extension.placeStages = [](Fenrir::App&, std::vector<Fenrir::StageGroup>, Fenrir::StageChanges) {};
            extension.removeStages = [](Fenrir::App&) {};
        }
        return extension;
    }
} // namespace

TEST_CASE("the stage rules: a stage declared twice, one with no name, or stages with no hooks fail the set",
          "[extension-set][stages]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    std::vector<const char*> expected;

    SECTION("one stage declared by two extensions")
    {
        extensions.push_back(Stages("first", {"Rules.Stage"}));
        extensions.push_back(Stages("second", {"Rules.Stage"}));
        expected = {"'Rules.Stage'", "'first'", "'second'"};
    }
    SECTION("one stage declared twice by one extension")
    {
        extensions.push_back(Stages("twice", {"Rules.Stage", "Rules.Stage"}));
        expected = {"'Rules.Stage'", "declared twice", "'twice'"};
    }
    SECTION("a stage with no name")
    {
        extensions.push_back(Stages("nameless", {""}));
        expected = {"'nameless'", "a stage with no name"};
    }
    SECTION("stages with no placing or removing")
    {
        extensions.push_back(Stages("hookless", {"Rules.Stage"}, false));
        expected = {"'hookless'", "placeStages and removeStages"};
    }

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("The set should be refused as InvalidArgument");
    REQUIRE((!installed.Succeeded() && installed.status == Iris::CallStatus::InvalidArgument));
    for (const char* part : expected)
    {
        INFO("The failure did not say " + std::string(part) + ": " + installed.diagnostic);
        REQUIRE(Contains(installed.diagnostic, part));
    }
}

TEST_CASE("the set keeps the extensions that declare stages, in extension order, and leaves out the rest",
          "[extension-set][stages]")
{
    RulesTest::FakeNativeRegistrar registrar;
    std::vector<ScriptingExtension> extensions;
    extensions.push_back(Stages("pose", {"Rules.Animate", "Rules.Modify"}));
    extensions.push_back(Extension("plain", {NativeFunction::Make<&Stateless>("Rules.Plain.Answer")}));
    extensions.push_back(Stages("camera", {"Rules.Follow"}));

    const auto installed = ExtensionSet::Install(std::move(extensions), registrar);

    INFO("The set should install: " + installed.diagnostic);
    REQUIRE(installed.Succeeded());
    const auto& stages = installed.value->Stages();
    INFO("Only the two extensions with stages should be kept, in order, with their stages and hooks");
    REQUIRE(stages.size() == 2);
    REQUIRE((stages[0].extension == "pose" && stages[0].stages == Names{"Rules.Animate", "Rules.Modify"}));
    REQUIRE((stages[1].extension == "camera" && stages[1].stages == Names{"Rules.Follow"}));
    REQUIRE((stages[0].place && stages[0].remove && stages[1].place && stages[1].remove));
}