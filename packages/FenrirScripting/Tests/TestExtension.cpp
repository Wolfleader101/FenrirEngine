// The test-only scripting extension's natives (TestExtension.hpp), and the extension as the tests install it.

#include "TestExtension.hpp"

#include "FixtureCalls.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/EntityValue.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace TestExtension
{
    std::int32_t Add(const Fenrir::CallContext&, std::int32_t left, std::int32_t right)
    {
        return left + right;
    }

    std::uint32_t EchoEntity(const Fenrir::CallContext&, std::uint32_t entity)
    {
        return entity;
    }

    // The entity's Transform height in the active scene, or -1 if it has none.
    float HeightOf(const Fenrir::CallContext& context, std::uint32_t entity)
    {
        const Fenrir::Entity found = context.Entities().GetEntity(entity);
        return found.IsValid() && found.HasComponent<Fenrir::Transform>()
                   ? found.GetComponent<Fenrir::Transform>().pos.y
                   : -1.0F;
    }

    void Lift(const Fenrir::CallContext& context, std::uint32_t entity, float by)
    {
        const Fenrir::Entity found = context.Entities().GetEntity(entity);
        if (found.IsValid() && found.HasComponent<Fenrir::Transform>())
        {
            found.GetComponent<Fenrir::Transform>().pos.y += by;
        }
    }

    std::string labelled;

    std::int32_t Label(const Fenrir::CallContext&, std::string_view text, std::int32_t number)
    {
        labelled = std::string(text);
        return static_cast<std::int32_t>(text.size()) * 10 + number;
    }

    // Declared for the broken contract's Mismatched field, which takes an int: this takes a float.
    std::int32_t Mismatched(const Fenrir::CallContext&, float)
    {
        return 0;
    }

    std::uint32_t SpawnCharged(const Fenrir::CallContext& context, std::int32_t level)
    {
        const std::uint32_t entity = context.Changes().CreateEntity();
        (void)context.Changes().AddComponent(entity, Charge{level});
        return entity;
    }

    void Rearrange(const Fenrir::CallContext& context, std::uint32_t kept, std::uint32_t doomed, float wind)
    {
        const auto changes = context.Changes();
        (void)changes.AddComponent(kept, Charge{1});
        (void)changes.RemoveComponent<Charge>(kept);
        (void)changes.AddComponent(kept, Charge{3});
        (void)changes.DestroyEntity(doomed);
        (void)changes.AddComponent(doomed, Charge{9});
        changes.InsertSceneResource(Weather{wind, 4});
    }

    std::uint32_t SpawnUnder(const Fenrir::CallContext& context, std::uint32_t parent)
    {
        const auto changes = context.Changes();
        const std::uint32_t child = changes.CreateEntity();
        (void)changes.AddChild(parent, child);
        return child;
    }

    std::int32_t AddToTally(const Fenrir::CallContext&, Tally& tally, std::int32_t by)
    {
        tally.total += by;
        return tally.total;
    }

    // Raises the entity's Transform by one, behind the native guard: a dead entity warns once under the native's name,
    // which this function never spells.
    void Nudge(const Fenrir::CallContext& context, Tally& tally, std::uint32_t entity)
    {
        const Fenrir::NativeGuard guard(context, tally.warnings);
        if (const std::optional<Fenrir::Entity> found = guard.Alive(Fenrir::EntityValue(entity)))
        {
            found->GetComponent<Fenrir::Transform>().pos.y += 1.0F;
        }
    }

    std::int32_t Weigh(const Fenrir::CallContext& context, std::uint32_t entity, Reading* reading)
    {
        const Fenrir::Entity found = context.Entities().GetEntity(entity);
        if (!found.IsValid())
        {
            return 0;
        }
        *reading = Reading{1, found.GetComponent<Fenrir::Transform>().pos.y};
        return 1;
    }

    Heading TurnRight(const Fenrir::CallContext&, Heading heading)
    {
        return heading == Heading::West ? Heading::North : static_cast<Heading>(static_cast<std::uint8_t>(heading) + 1);
    }

    Tag Retag(const Fenrir::CallContext&, Tag tag, Heading by)
    {
        return Tag{static_cast<std::uint16_t>(tag.code + static_cast<std::uint16_t>(by))};
    }
} // namespace TestExtension

namespace Fenrir::Testing::ScriptFixture
{
    ScriptingExtension TheTestExtension(std::shared_ptr<TestExtension::Tally> tally)
    {
        ScriptingExtension extension;
        extension.name = "test extension";
        extension.sharedAssemblies = {testExtension};
        extension.functions = {
            NativeFunction::Make<&TestExtension::Add>("Fenrir.TestExtension.Gadgets.Add"),
            NativeFunction::Make<&TestExtension::EchoEntity>("Fenrir.TestExtension.Gadgets.EchoEntity"),
            NativeFunction::Make<&TestExtension::HeightOf>("Fenrir.TestExtension.Gadgets.HeightOf"),
            NativeFunction::Make<&TestExtension::Lift>("Fenrir.TestExtension.Gadgets.Lift"),
            NativeFunction::Make<&TestExtension::Label>("Fenrir.TestExtension.Gadgets.Label"),
            NativeFunction::Make<&TestExtension::SpawnCharged>("Fenrir.TestExtension.Gadgets.SpawnCharged"),
            NativeFunction::Make<&TestExtension::Rearrange>("Fenrir.TestExtension.Gadgets.Rearrange"),
            NativeFunction::Make<&TestExtension::SpawnUnder>("Fenrir.TestExtension.Gadgets.SpawnUnder"),
            NativeFunction::Make<&TestExtension::AddToTally>("Fenrir.TestExtension.Gadgets.Tally"),
            NativeFunction::Make<&TestExtension::TurnRight>("Fenrir.TestExtension.Gadgets.TurnRight"),
            NativeFunction::Make<&TestExtension::Retag>("Fenrir.TestExtension.Gadgets.Retag"),
            NativeFunction::Make<&TestExtension::Nudge>("Fenrir.TestExtension.Gadgets.Nudge"),
            // As the generated registration names a native with an output (ADR-0045).
            NativeFunction::Make<&TestExtension::ZeroingWeigh::Call>("Fenrir.TestExtension.Gadgets.Weigh", -1),
        };
        extension.components = {MakeComponent<TestExtension::Charge>("Fenrir.TestExtension.Charge")};
        extension.events = {MakeEvent<TestExtension::Ping>("Fenrir.TestExtension.Ping")};
        extension.sharedStructs = {MakeSharedStruct<TestExtension::Reading>("Fenrir.TestExtension.Reading")};
        extension.resources = {
            MakeSceneResource<TestExtension::Weather>("Fenrir.TestExtension.Weather"),
            MakeAppResource<TestExtension::Settings>("Fenrir.TestExtension.Settings"),
            MakeReadOnlyAppResource<TestExtension::Clock>("Fenrir.TestExtension.Clock"),
        };
        if (tally != nullptr)
        {
            extension.SetState(std::move(tally));
        }
        return extension;
    }

    ScriptingConfig ExtensionConfig(std::vector<ScriptingExtension> extensions)
    {
        ScriptingConfig config = ConfigFor(extensionScript);
        config.extensions = std::move(extensions);
        return config;
    }
} // namespace Fenrir::Testing::ScriptFixture
