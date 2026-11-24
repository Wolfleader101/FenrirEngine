// The goldens rewritten as a tool rewrites a file (the spec, "The bridge"): ReadText, FromTree of each typed value into
// its hand-registered type, one field changed, ToTree with the value's own subtree as `prior`, then WriteText. Every
// typed value in the file goes through the bridge, so the bytes differ from the golden's only in the changed value.
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
using SerialisationTest::TreeOf;

namespace
{
    // `text` with its first `from` replaced by `to`; unchanged when `from` is not in it.
    std::string Replaced(std::string_view text, std::string_view from, std::string_view to)
    {
        std::string result(text);
        const std::size_t at = result.find(from);
        if (at != std::string::npos)
            result.replace(at, from.size(), to);
        return result;
    }

    // Replaces `node`, a `T` of type `type`, with the tree of the value it reads as after `change`, written with
    // `node` as its prior tree.
    template <typename T>
    void Rewrite(Value& node, const Fenrir::TypeDescription& type, const Fenrir::TypeRegistry& registry,
                 const std::function<void(T&)>& change = {})
    {
        T value;
        RecordingLogger logger;
        (void)FromTree(node, type, registry, Resolvers{}, &value, "golden.json5", logger);
        if (change)
            change(value);
        node = ToTree(&value, type, registry, Resolvers{}, &node);
    }

    // The component or resource `object` holds under `type`'s name, rewritten as `T`.
    template <typename T>
    void RewriteMember(Object& object, const Fenrir::TypeDescription& type, const Fenrir::TypeRegistry& registry,
                       const std::function<void(T&)>& change = {})
    {
        if (Value* node = object.Find(type.name))
            Rewrite<T>(*node, type, registry, change);
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
    Value tree = TreeOf(golden, "sword.asset.json5");

    Object& envelope = tree.AsObject();
    Rewrite<Sword>(*envelope.Find("data"), SwordDescription, registry, [](Sword& sword) { sword.damage = 20.0f; });
    Rewrite<ParticleEffect>(*envelope.Find("sub")->AsObject().Find("sparks")->AsObject().Find("data"),
                            ParticleEffectDescription, registry);

    INFO("the sword and its sparks written by ToTree with prior give the golden's bytes, but for `damage: 20`");
    REQUIRE(WriteText(tree) == Replaced(golden, "damage: 12.5,", "damage: 20,"));
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

    Object& envelope = tree.AsObject();
    RewriteMember<Sky>(envelope.Find("resources")->AsObject(), SkyDescription, registry);
    for (Element& entity : envelope.Find("entities")->AsArray().elements)
    {
        Object& components = entity.value.AsObject().Find("components")->AsObject();
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
