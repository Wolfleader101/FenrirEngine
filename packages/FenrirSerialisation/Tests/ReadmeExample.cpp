// The package README's save file example (README.md, "A game's save file"): its C++ block, copied out of the README
// when CMake configures, is compiled here and run, so the README shows doors that compose as it says.
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "SerialisationTest.hpp"

#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MyGame
{
    // The player's progress at version 2, as the README describes it; at version 1, `health` was `hp`.
    struct Progress
    {
        std::uint32_t level = 1;
        float health = 100.0f;
    };
} // namespace MyGame

namespace
{
    const Fenrir::FieldInfo ProgressFields[] = {
        {.name = "level", .type = {.kind = Fenrir::Kind::U32}, .offset = offsetof(MyGame::Progress, level)},
        {.name = "health", .type = {.kind = Fenrir::Kind::F32}, .offset = offsetof(MyGame::Progress, health)},
    };

    const Fenrir::TypeDescription ProgressDescription{
        .name = "MyGame::Progress",
        .package = "FenrirSerialisationTests",
        .role = Fenrir::Role::SharedStruct,
        .version = 2,
        .size = sizeof(MyGame::Progress),
        .alignment = alignof(MyGame::Progress),
        .construct = Fenrir::ConstructValue<MyGame::Progress>,
        .destroy = Fenrir::DestroyValue<MyGame::Progress>,
        .fields = ProgressFields,
    };
} // namespace

// What the game's generated header would give: the compile-time door to `Progress`'s description.
template <>
struct Fenrir::Describe<MyGame::Progress>
{
    static const TypeDescription& Description()
    {
        return ProgressDescription;
    }
};

// The README's block, as CMake copied it.
#include "ReadmeExample.inl"

using namespace Fenrir::Serialisation;
using SerialisationTest::RecordingLogger;

namespace
{
    // A registry holding `Progress` with its upgraders, as the game's start-up leaves it.
    Fenrir::TypeRegistry GameRegistry(RecordingLogger& logger)
    {
        Fenrir::TypeRegistry registry;
        registry.Register(ProgressDescription);
        const bool registered = MyGame::RegisterProgressUpgraders(registry, logger);
        INFO("the README's upgrader registers");
        REQUIRE(registered);
        return registry;
    }

    // The bytes of `text`, as a file holding it stores them.
    std::span<const std::byte> BytesOf(std::string_view text)
    {
        return std::as_bytes(std::span(text.data(), text.size()));
    }

    // The first error logged, or "no error", for an INFO.
    std::string FirstError(const RecordingLogger& logger)
    {
        return logger.errors.empty() ? std::string("no error") : logger.errors.front();
    }
} // namespace

TEST_CASE("The README's save file writes the progress under the game's own keys", "[serialisation][readme]")
{
    RecordingLogger logger;
    const Fenrir::TypeRegistry registry = GameRegistry(logger);
    const std::string text = MyGame::WriteSave(MyGame::Progress{.level = 7, .health = 42.5f}, registry);
    INFO("the save holds the type's version and the fields that differ from their defaults:\n" << text);
    REQUIRE(text == "{\n  progressVersion: 2,\n  progress: {\n    level: 7,\n    health: 42.5,\n  },\n}\n");
}

TEST_CASE("The README's save file reads back the progress it wrote", "[serialisation][readme]")
{
    RecordingLogger logger;
    const Fenrir::TypeRegistry registry = GameRegistry(logger);
    const std::string text = MyGame::WriteSave(MyGame::Progress{.level = 7, .health = 42.5f}, registry);

    MyGame::Progress progress;
    const bool read = MyGame::ReadSave(BytesOf(text), "save.json5", registry, progress, logger);
    INFO("the save reads: " << FirstError(logger));
    REQUIRE(read);
    INFO("each field reads as written");
    REQUIRE(progress.level == 7);
    REQUIRE(progress.health == 42.5f);
    INFO("nothing was logged");
    REQUIRE(logger.errors.empty());
    REQUIRE(logger.warnings.empty());
}

TEST_CASE("The README's save file reads a save cooked into the tree encoding", "[serialisation][readme]")
{
    RecordingLogger logger;
    const Fenrir::TypeRegistry registry = GameRegistry(logger);
    const std::string text = MyGame::WriteSave(MyGame::Progress{.level = 7, .health = 42.5f}, registry);
    const std::vector<std::byte> cooked = EncodeTree(SerialisationTest::TreeOf(text), Trivia::Drop);

    MyGame::Progress progress;
    const bool read = MyGame::ReadSave(cooked, "save.bin", registry, progress, logger);
    INFO("the encoded save reads through the same function: " << FirstError(logger));
    REQUIRE(read);
    INFO("each field reads as written");
    REQUIRE(progress.level == 7);
    REQUIRE(progress.health == 42.5f);
}

TEST_CASE("The README's save file upgrades a version 1 save before reading it", "[serialisation][readme]")
{
    RecordingLogger logger;
    const Fenrir::TypeRegistry registry = GameRegistry(logger);

    MyGame::Progress progress;
    const bool read = MyGame::ReadSave(BytesOf("{progressVersion: 1, progress: {level: 3, hp: 12}}"), "old.json5",
                                       registry, progress, logger);
    INFO("the old save reads: " << FirstError(logger));
    REQUIRE(read);
    INFO("the field written as `hp` at version 1 reads as `health`");
    REQUIRE(progress.health == 12.0f);
    INFO("a field the upgrader did not touch reads as written");
    REQUIRE(progress.level == 3);
}

TEST_CASE("The README's save file refuses a file with no progress in it, with one error", "[serialisation][readme]")
{
    RecordingLogger logger;
    const Fenrir::TypeRegistry registry = GameRegistry(logger);

    MyGame::Progress progress;
    const bool read = MyGame::ReadSave(BytesOf("{level: 3}"), "notasave.json5", registry, progress, logger);
    INFO("a file without `progressVersion` and `progress` is no save");
    REQUIRE_FALSE(read);
    INFO("one error says so");
    REQUIRE(logger.errors.size() == 1);
    INFO("the progress keeps its defaults");
    REQUIRE(progress.level == 1);
}
