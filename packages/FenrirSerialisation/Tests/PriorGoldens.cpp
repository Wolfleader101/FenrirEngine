// The goldens rewritten as a tool rewrites a file (the spec, "The bridge"): ReadText, FromTree of each typed value into
// its hand-registered type, one field changed, ToTree with the value's own subtree as `prior`, then WriteText. Every
// typed value in the file goes through the bridge, so the bytes differ from the golden's only in the changed value.
// The goldens' one comment is on the envelope, which no type describes, so the sword is rewritten a second time with
// comments added inside its typed values.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "GoldenTypes.hpp"
#include "SerialisationTest.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <string_view>

using namespace Fenrir::Serialisation;
using namespace GoldenTest;
using SerialisationTest::ReadFixture;
using SerialisationTest::RecordingLogger;
using SerialisationTest::Replaced;
using SerialisationTest::TreeOf;

namespace
{
    // The value of `object`'s member `key`, an object; the golden must hold it there.
    Value& MemberOf(Value& object, std::string_view key)
    {
        Value* member = object.Kind() == ValueKind::Object ? object.AsObject().Find(key) : nullptr;
        INFO("the golden holds a member `" << key << "`");
        REQUIRE(member != nullptr);
        return *member;
    }

    // Replaces `node`, a `T` of type `type`, with the tree of the value it reads as after `change`, written with
    // `node` as its prior tree. The read must succeed, so a failure is in the rewrite.
    template <typename T>
    void Rewrite(Value& node, const Fenrir::TypeDescription& type, const Fenrir::TypeRegistry& registry,
                 const std::function<void(T&)>& change = {})
    {
        T value;
        RecordingLogger logger;
        const BridgeResult read = FromTree(node, type, registry, Resolvers{}, &value, "golden.json5", logger);
        {
            INFO("the golden's " << type.name << " reads with nothing defaulted, unknown or unresolved");
            REQUIRE(read.status == BridgeStatus::Succeeded);
        }
        if (change)
            change(value);
        node = ToTree(&value, type, registry, Resolvers{}, &node);
    }

    // The component or resource `object` holds under `type`'s name, if it holds one, rewritten as `T`.
    template <typename T>
    void RewriteMember(Value& object, const Fenrir::TypeDescription& type, const Fenrir::TypeRegistry& registry,
                       const std::function<void(T&)>& change = {})
    {
        if (Value* node = object.AsObject().Find(type.name))
            Rewrite<T>(*node, type, registry, change);
    }

    // The sword file `text` holds, with its sword's damage set to 20 and its sparks rewritten unchanged.
    std::string RewrittenSword(std::string_view text, const Fenrir::TypeRegistry& registry)
    {
        Value tree = TreeOf(text, "sword.asset.json5");
        Rewrite<Sword>(MemberOf(tree, "data"), SwordDescription, registry, [](Sword& sword) { sword.damage = 20.0f; });
        Rewrite<ParticleEffect>(MemberOf(MemberOf(MemberOf(tree, "sub"), "sparks"), "data"), ParticleEffectDescription,
                                registry);
        return WriteText(tree);
    }
} // namespace

TEST_CASE("sword.asset.json5 rewritten through the bridge with a new damage differs from the golden only there",
          "[serialisation][golden][totree][prior]")
{
    Fenrir::TypeRegistry registry;
    {
        const bool registered = RegisterGoldenTypes(registry);
        INFO("FenrirMath's types and the goldens' register without a refusal");
        REQUIRE(registered);
    }
    const std::string golden = ReadFixture("sword.asset.json5");

    INFO("the sword and its sparks written by ToTree with prior give the golden's bytes, but for `damage: 20`");
    REQUIRE(RewrittenSword(golden, registry) == Replaced(golden, "damage: 12.5,", "damage: 20,"));
}

TEST_CASE("The sword golden with comments in its typed values keeps every one through a rewrite with a new damage",
          "[serialisation][golden][totree][prior]")
{
    Fenrir::TypeRegistry registry;
    {
        const bool registered = RegisterGoldenTypes(registry);
        INFO("FenrirMath's types and the goldens' register without a refusal");
        REQUIRE(registered);
    }
    std::string commented = ReadFixture("sword.asset.json5");
    commented =
        Replaced(commented, "    damage: 12.5,\n", "    // tuned for the first act\n    damage: 12.5, // per hit\n");
    commented = Replaced(commented, "      {level: 2,", "      // the second upgrade\n      {level: 2,");
    commented =
        Replaced(commented, "    onHit: \"#sparks\",\n", "    onHit: \"#sparks\",\n    // more upgrades later\n");
    commented = Replaced(commented, "        count: 24,\n", "        count: 24, // sparks per hit\n");

    INFO("the comments on the sword's members, an upgrade element, the data's close and the sparks stay where they "
         "were, and only the damage differs");
    REQUIRE(RewrittenSword(commented, registry) == Replaced(commented, "damage: 12.5,", "damage: 20,"));
}

TEST_CASE("level1.scene.json5 rewritten through the bridge with a new sun intensity differs from the golden only there",
          "[serialisation][golden][totree][prior]")
{
    Fenrir::TypeRegistry registry;
    {
        const bool registered = RegisterGoldenTypes(registry);
        INFO("FenrirMath's types and the goldens' register without a refusal");
        REQUIRE(registered);
    }
    const std::string golden = ReadFixture("level1.scene.json5");
    Value tree = TreeOf(golden, "level1.scene.json5");

    RewriteMember<Sky>(MemberOf(tree, "resources"), SkyDescription, registry);
    for (Element& entity : MemberOf(tree, "entities").AsArray().elements)
    {
        Value& components = MemberOf(entity.value, "components");
        RewriteMember<Name>(components, NameDescription, registry);
        RewriteMember<Transform>(components, TransformDescription, registry);
        RewriteMember<MeshRenderer>(components, MeshRendererDescription, registry);
        RewriteMember<BoneAttachment>(components, BoneAttachmentDescription, registry);
        RewriteMember<DirectionalLight>(components, DirectionalLightDescription, registry,
                                        [](DirectionalLight& light) { light.intensity = 4.0f; });
    }

    INFO("every component and the sky written by ToTree with prior give the golden's bytes, but for `intensity: 4`");
    REQUIRE(WriteText(tree) == Replaced(golden, "intensity: 3.5,", "intensity: 4,"));
}
